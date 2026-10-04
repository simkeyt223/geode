#include "main.hpp"
#include "PropsCache.hpp"
#include <Geode/Geode.hpp>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace geode::prelude;

namespace {

// Waits for `done()` by spinning, then yielding. Never sleeps for a fixed time:
// a short sleep can take ~15ms on Windows, which is a whole frame.
// Only used by the main thread, which waits on a worker that is already running.
template<typename F>
void waitUntil(F&& done) {
	for (int spins = 0; !done(); ++spins) {
		if (spins < 256) continue;
		std::this_thread::yield();
	}
}

// A single-producer (main thread) / single-consumer (worker) ring of parse jobs.
// The worker parses object strings ahead of the main thread, which only has to create the objects.
class PropsComputer {
public:
	static constexpr size_t N = 8; // slots; at most N-1 may be queued ahead

private:
	struct Job {
		std::atomic<bool> ready{false};
		// input
		gd::string const* objStr{nullptr};
		// output
		gd::vector<gd::string> strs{}; // needed because of an explicit constructor error on Android
		gd::vector<void*> games{}; // ditto
		PropsCache cache;
	};
	std::thread computeThread; // poor support for `std::jthread` :(
	GJBaseGameLayer* gameLayer{nullptr};
	std::atomic<bool> stop{false};
	std::atomic<bool> sleeping{false};
	std::mutex sleepMutex;
	std::condition_variable sleepCv;
	bool active{false};
	std::array<Job, N> jobs{};
	size_t jq{0}; // next slot to queue into (main thread)
	size_t jf{0}; // next slot to fetch from (main thread)
	size_t outstanding{0}; // queued but not yet fetched (main thread)

	void process() {
		size_t j = 0;
		for (;;) {
			auto& job = jobs[j];
			if (!(job.ready.load() || stop.load())) {
				// brief spin first: the next job is usually queued within microseconds
				for (int s = 0; s < 4000 && !(job.ready.load() || stop.load()); ++s) {
					if (s >= 200) std::this_thread::yield();
				}
				if (!(job.ready.load() || stop.load())) {
					// really idle (e.g. between frames): block until woken, no timed sleeps
					std::unique_lock lock{sleepMutex};
					sleeping.store(true);
					sleepCv.wait(lock, [&] { return job.ready.load() || stop.load(); });
					sleeping.store(false);
				}
			}
			if (stop.load()) break;
			auto str = *job.objStr;
			// this is a lot different than the decompilation, hope it's the same
			std::ranges::fill(job.games, nullptr);
			job.cache = {};
			auto begin = str.data();
			auto end = begin + str.size();
			for (auto it = begin; it != end; ++it) {
				if (*it == ',') *it = '\0';
			}
			auto it = begin;
			while (true) {
				auto pos = atoi(it);
				while (*it != '\0') ++it;
				if (it == end) break;
				++it;
				if (0 < pos && pos < 600) {
					#ifdef GEODE_IS_ANDROID
					// otherwise the old string's refcount gets decremented twice due to a Geode bug, I think.
					// this seems to only manifest with the assignment of `m_particleData` in `ParticleGameObject::customObjectSetup`,
					// because the copy-on-write policy causes the string to be shared, and it isn't immediately processed
					job.strs[pos].clear();
					#endif
					job.strs[pos] = it;
					job.games[pos] = gameLayer;
					job.cache.cacheProp(pos, it);
				}
				while (*it != '\0') ++it;
				if (it == end) break;
				++it;
			}
			job.ready.store(false);
			j = (j + 1) % N;
		}
	}

public:
	~PropsComputer() { tryFinish(); } // a joinable std::thread at exit would std::terminate

	bool isActive() const { return active; }

	void start(GJBaseGameLayer* layer) {
		gameLayer = layer;
		stop = false;
		active = true;
		for (auto& job : jobs) {
			job.ready = false;
			job.strs.resize(600);
			job.games.resize(600);
		}
		jq = 0;
		jf = 0;
		outstanding = 0;
		computeThread = std::thread{&PropsComputer::process, this};
	}

	// wakes the worker if (and only if) it went to sleep
	void wake() {
		if (sleeping.load()) {
			{ std::lock_guard lock{sleepMutex}; } // ensures the worker is really waiting before we notify
			sleepCv.notify_one();
		}
	}

	// number of jobs that can still be queued right now
	size_t freeSlots() const { return N - 1 - outstanding; }

	// user's responsibility to not over-call (see `freeSlots`)
	void queue(gd::string const& str) {
		jobs[jq].objStr = &str;
		jobs[jq].ready.store(true);
		wake();
		jq = (jq + 1) % N;
		++outstanding;
	}

	// user's responsibility to not over-call
	Job& fetch() {
		auto& job = jobs[jf];
		waitUntil([&] { return !job.ready.load(); });
		jf = (jf + 1) % N;
		--outstanding;
		return job;
	}

	// throw away anything still queued (used when a batch ends early)
	void drain() {
		while (outstanding > 0) fetch();
	}

	void finish() {
		stop.store(true);
		wake();
		if (computeThread.joinable()) computeThread.join();
		computeThread = {};
		active = false;
		outstanding = 0;
		for (auto& job : jobs) {
			job.ready = false;
			job.strs.clear();
			job.games.clear();
		}
	}

	void tryFinish() {
		if (!active) return;
		finish();
	}

	// always begins from a clean state, even if a previous load was abandoned half way
	void restart(GJBaseGameLayer* layer) {
		tryFinish();
		start(layer);
	}

	void tryStart(GJBaseGameLayer* layer) {
		if (active) return;
		start(layer);
	}
};

PropsComputer pc; // global, whatever

}

// should be the same as original, besides the usage of `PropsCache` and `PropsComputer`,
// and the ability to stop early once `levelload::deadline` has passed
void PlayedLayer::processCreateObjectsFromSetup() {
	using clock = std::chrono::steady_clock;
	if (!levelload::fastMode) {
		if (m_objectsCreated == 0) levelload::stats = {clock::now(), {}, {}};
		PlayLayer::processCreateObjectsFromSetup();
		if (m_loadingProgress >= 1.f) {
			log::info("[timing] (fast mode off) {} objects, {:.0f} ms since load started", m_objectStrings.size(), levelload::ms(clock::now() - levelload::stats.start));
		}
		return;
	}
	auto const callStart = clock::now();

	auto n = m_objectStrings.size();
	auto kilos = n / 1000; // this took a bit to reverse
	auto frac = std::min(100.f, std::roundf(kilos) + 10.f);
	int numToCreate = std::ceil(n / frac);

	// ull and int comparison, can't merge in some cases (said cases raise bigger questions though)
	int const first = m_objectsCreated;
	auto inRange = [&](int i) {
		return i < n && i < first + numToCreate;
	};

	int i = first;

	// this should never be false (except empty levels maybe), but what if it is!
	if (inRange(first)) {

		// a fresh load always starts at 0; this also recovers from a previously abandoned load
		if (first == 0) {
			pc.restart(this);
			levelload::stats = {callStart, {}, {}};
		} else pc.tryStart(this);

		int nextToQueue = first;
		auto lowDetail = m_level->m_lowDetailModeToggled;
		auto const& deadline = levelload::deadline;

		for (; inRange(i); ++i) {

			// keep the parsing thread as far ahead as the ring allows
			while (inRange(nextToQueue) && pc.freeSlots() > 0) {
				pc.queue(m_objectStrings[nextToQueue]);
				++nextToQueue;
			}

			auto const waitStart = clock::now();
			auto& job = pc.fetch();
			levelload::stats.wait += clock::now() - waitStart;

			auto obj = GamedObject::newObjectFromVector(job.strs, job.games, this, lowDetail, job.cache);
			if (!obj) continue;

			if (obj->m_objectID == 31) {
				static_cast<StartPosObject*>(obj)->loadSettingsFromString(*job.objStr);
			} else if (obj->m_objectID == 2065) {
				static_cast<ParticleGameObject*>(obj)->updateParticleStruct();
			}
			if (obj->getType() == GameObjectType::SecretCoin && m_level->m_levelType != GJLevelType::Main) continue;
			if (obj->m_mainColorKeyIndex < 1) {
				auto colors = 1 + obj->hasSecondaryColor();
				for (int c = 0; c < colors; ++c) {
					auto key = obj->getColorKey(c == 0, false);
					auto color = static_cast<CCInteger*>(m_colorKeyDict->objectForKey(key));
					int value;
					if (color) {
						value = color->m_nValue;
					} else {
						value = m_nextColorKey;
						m_colorKeyDict->setObject(CCInteger::create(value), key);
						++m_nextColorKey;
					}
					(c == 0 ? obj->m_mainColorKeyIndex : obj->m_detailColorKeyIndex) = value;
				}
			} else {
				m_nextColorKey = std::max({
					m_nextColorKey,
					obj->m_mainColorKeyIndex,
					obj->m_detailColorKeyIndex,
				});
			}
			if (obj->getType() == GameObjectType::UserCoin) {
				if (m_coinArray->count() < 3) {
					m_coinArray->addObject(obj);
					addObject(obj);
				}
			} else {
				addObject(obj);
			}

			// out of time for this frame? stop at a clean boundary (checking the clock is cheap but not free)
			if (deadline && ((i - first) & 15) == 15 && std::chrono::steady_clock::now() >= *deadline) {
				++i;
				break;
			}
		}

		// anything queued beyond where we stopped has to be consumed so the ring is empty again
		pc.drain();
	}

	// `i` is the number of objects handled so far (equals first + numToCreate unless we stopped early)
	m_objectsCreated = i;
	m_loadingProgress = std::min(1.f, (m_objectsCreated - 1.f) / m_objectStrings.size());
	if (m_objectsCreated < m_objectStrings.size()) {
		levelload::stats.create += clock::now() - callStart;
		return;
	}
	levelload::stats.create += clock::now() - callStart;
	pc.tryFinish();
	auto const t1 = clock::now();
	createObjectsFromSetupFinished();
	auto const t2 = clock::now();
	m_loadingProgress = 1.f;
	setupHasCompleted();
	auto const t3 = clock::now();
	auto const& s = levelload::stats;
	log::info("[timing] {} objects | {:.0f} ms since start | object loop {:.0f} ms (of which waiting on parse thread {:.0f} ms) | createObjectsFromSetupFinished {:.0f} ms | setupHasCompleted {:.0f} ms",
		m_objectStrings.size(), levelload::ms(t3 - s.start), levelload::ms(s.create), levelload::ms(s.wait), levelload::ms(t2 - t1), levelload::ms(t3 - t2));
}
