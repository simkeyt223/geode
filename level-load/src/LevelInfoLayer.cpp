#include "main.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

void LeveledInfoLayer::loadLevelStep() $override {
	using clock = std::chrono::steady_clock;
	auto const start = clock::now();
	auto interval = std::chrono::duration<double>(CCDirector::get()->getAnimationInterval());
	auto percent = static_cast<double>(Mod::get()->getSettingValue<int64_t>("frame-budget"));
	auto budget = std::chrono::duration_cast<clock::duration>(interval * (percent / 100.0));
	// always do at least a little work so loading can never stall
	if (budget < std::chrono::microseconds(500)) budget = std::chrono::microseconds(500);

	auto play = static_cast<PlayLayer*>(m_playScene->getChildren()->objectAtIndex(0)); // original
	// object creation checks this deadline every few objects, so a frame never overruns by a whole chunk
	levelload::deadline = start + budget;
	do {
		play->processCreateObjectsFromSetup(); // original
	} while (clock::now() < *levelload::deadline && play->m_loadingProgress < 1.f);
	levelload::deadline.reset();

	// rest is original
	m_progressTimer->setPercentage(play->m_loadingProgress * 100.f);

	runAction(CCSequence::create(
		CCDelayTime::create(0.f),
		CCCallFunc::create(this, static_cast<SEL_CallFunc>(
			play->m_loadingProgress < 1.f
			? &LevelInfoLayer::loadLevelStep
			: &LevelInfoLayer::playStep4
		)),
		nullptr
	));
}
