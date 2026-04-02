################################################################################
#                                                                              #
#  Copyright (C) 2026, goatpig                                                 #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################
#
# Setup Manager package - exports DlgSetupManager dialog
#

from qtdialogs.setupmanager.DlgSetupManager import DlgSetupManager
from qtdialogs.setupmanager.DatabaseTab import SCENARIO_DB_NONE

__all__ = ['DlgSetupManager', 'SCENARIO_DB_NONE']
