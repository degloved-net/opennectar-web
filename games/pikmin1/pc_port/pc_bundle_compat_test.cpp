#include "pc_bundle_compat.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
	char logoPath[] = "cinemas/titles/logoMotioe/logo.dck";
	assert(pcCanonicalizeBundleAnimationPath(logoPath));
	assert(std::strcmp(logoPath, "cinemas/titles/logoMotion/logo.dck") == 0);

	char ordinaryPath[] = "cinemas/demo01/motion/d1_ufo.dck";
	assert(!pcCanonicalizeBundleAnimationPath(ordinaryPath));
	assert(std::strcmp(ordinaryPath, "cinemas/demo01/motion/d1_ufo.dck") == 0);
	assert(!pcCanonicalizeBundleAnimationPath(nullptr));

	std::puts("bundle animation paths canonicalized");
	return 0;
}
