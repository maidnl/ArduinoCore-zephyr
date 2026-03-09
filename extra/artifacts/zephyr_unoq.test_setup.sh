# Copyright (c) Arduino s.r.l. and/or its affiliated companies
# SPDX-License-Identifier: Apache-2.0

# This script is sourced from extra/ci_test_list.sh to provide
# artifact-specific tests for Zephyr CI tests.
#
# Two helper functions are provided for easy GitHub queries:
# - get_branch_tip <folder> <repo> <branch> [<path> ...]
# - get_latest_release <folder> <repo> [<path> ...]
#
# By default, the whole project will be added to the test suite.
# When given additional path arguments, the functions will only
# copy artifacts under the provided paths.

# ArduinoBLE
get_branch_tip libraries pillo79/ArduinoBLE pr-fix-hci-overlap \
	examples/Central/LedControl \
	examples/Central/Scan \
	examples/Peripheral/Advertising/EnhancedAdvertising \
	examples/Peripheral/ButtonLED \

# Arduino_RouterBridge
get_branch_tip libraries pillo79/Arduino_RouterBridge singleton_in_cpp
