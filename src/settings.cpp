#include "main.hpp"

$execute {
	// "fast-mode" is requires-restart, so reading it once is enough
	levelload::fastMode = Mod::get()->getSettingValue<bool>("fast-mode");
	log::info("Fast object creation: {}", levelload::fastMode ? "on" : "off");
}
