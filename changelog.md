## 1.2.1
- Fix very slow loading on Windows: the parsing thread no longer uses timed sleeps (which can last ~15ms); it is woken directly
- Loading time per frame now defaults to 100%

## 1.2.0
- Update to Geode v5.10.1 and Geometry Dash 2.2081 (C++23)
- Loading is now time-sliced in small batches, so a frame can no longer overrun by a whole chunk (fixes freezing/stutter on huge levels)
- New setting: loading time per frame
- New setting: fast object creation (turn off as a safe fallback to the game's own code)
- Parsing thread no longer busy-spins a CPU core; it waits efficiently (helps phones and low-core CPUs)
- Parsing thread uses a deeper queue so it stays ahead of the main thread
- Fix: leaving a level mid-load no longer leaves the parsing thread in a stale state for the next load
- Fix: no crash at game exit if a load was abandoned

## 1.1.3
- Fix an Android crash affecting levels containing ParticleGameObject
- Update to Geode v4.8.0

## 1.1.2
- Compatibility: prevent crash if processCreateObjectsFromSetup is called extraneously

## 1.1.1
- Incorporate completed bindings (now supports all platforms)
- Fix minor bugs

## 1.1.0
- Multithread parsing objects
- Avoid idle time when loading an online level

## 1.0.0
- Initial release
