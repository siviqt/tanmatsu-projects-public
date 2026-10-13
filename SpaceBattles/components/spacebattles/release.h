// SPDX-License-Identifier: MIT
#pragma once
// Increment by one only after explicit release approval from the owner.
#define SB_RELEASE_NUMBER 6
#define SB_RELEASE_STRINGIFY_(n) #n
#define SB_RELEASE_STRINGIFY(n) SB_RELEASE_STRINGIFY_(n)
#define SB_RELEASE_LABEL "Release #" SB_RELEASE_STRINGIFY(SB_RELEASE_NUMBER)
