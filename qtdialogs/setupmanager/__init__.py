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

from qtdialogs.setupmanager.DlgSetupManager import DlgSetupManager, DlgAutomations
from qtdialogs.setupmanager.DatabaseTab import SCENARIO_DB_OFFLINE

__all__ = ['DlgSetupManager', 'DlgAutomations', 'SCENARIO_DB_OFFLINE']
