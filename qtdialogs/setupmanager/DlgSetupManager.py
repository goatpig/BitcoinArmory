################################################################################
#                                                                              #
#  Copyright (C) 2025, goatpig                                                 #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

from qtpy import QtCore, QtWidgets
from armoryengine.ArmoryUtils import CLI_OPTIONS
from armoryengine.Settings import TheSettings

from qtdialogs.ArmoryDialog import ArmoryDialog
import qtdialogs.qtdefines as qtdefines

from qtdialogs.setupmanager.WalletTab import WalletTab
from qtdialogs.setupmanager.CoreTab import CoreTab
from qtdialogs.setupmanager.DatabaseTab import DatabaseTab, SCENARIO_DB_LOCAL, \
   SCENARIO_DB_REMOTE

# Dialog-specific constants (only used by this dialog)
MINIMUM_DIALOG_WIDTH = 300
MINIMUM_DIALOG_HEIGHT = 500

################################################################################
class DlgSetupManager(ArmoryDialog):
   """
   Setup Manager Dialog for Armory configuration.

   Orchestrates the three settings tabs:
   - Wallet settings and migration
   - Bitcoin Core configuration
   - Database settings (local/remote/none)
   """
   def __init__(self, parent=None, main=None):
      """Initialize the setup manager dialog."""
      super().__init__(parent, main)
      self.tabWidget = None
      self.walletTab = None
      self.coreTab = None
      self.databaseTab = None
      self.acceptButton = None
      self.cancelButton = None
      self.bottomFrame = None
      self.setupDialogProperties()
      self.initTabs()
      self.setupMainLayout()
      self.loadSettings()
      self.connectSignals()

   def setupDialogProperties(self):
      """Configure basic dialog properties and styling."""
      self.setMinimumWidth(MINIMUM_DIALOG_WIDTH)
      self.setMinimumHeight(MINIMUM_DIALOG_HEIGHT)
      self.setWindowTitle(self.tr('Armory Setup Manager'))
      self.setWindowFlags(QtCore.Qt.Window)
      self.setModal(True)
      qtdefines.applyDialogBaseStyle(self)

   def initTabs(self):
      """Initialize all tab widgets."""
      self.tabWidget = QtWidgets.QTabWidget()
      self.tabWidget.setContentsMargins(14, 6, 14, 8)
      self.walletTab = WalletTab(self, self.main)
      self.coreTab = CoreTab(self, self.main)
      self.databaseTab = DatabaseTab(self, self.main)
      self.tabWidget.addTab(self.walletTab, self.tr('Wallet Settings'))
      self.tabWidget.addTab(self.coreTab, self.tr('Core Settings'))
      self.tabWidget.addTab(self.databaseTab, self.tr('Database Settings'))
      if CLI_OPTIONS.offline:
         self.tabWidget.setTabEnabled(1, False)
         self.tabWidget.setTabEnabled(2, False)
         self.tabWidget.setTabText(1, self.tr('Core Settings (Offline)'))
         self.tabWidget.setTabText(2, self.tr('Database Settings (Offline)'))

   def setupMainLayout(self):
      """Set up the main layout with tabs and buttons."""
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setSpacing(8)
      mainLayout.setContentsMargins(0, 0, 0, 0)
      mainLayout.addWidget(self.tabWidget)
      self.createBottomButtonFrame()
      mainLayout.addWidget(self.bottomFrame)
      self.setLayout(mainLayout)

   def createBottomButtonFrame(self):
      """Create the bottom frame containing Accept/Cancel buttons."""
      self.bottomFrame = QtWidgets.QFrame()
      bottomLayout = QtWidgets.QHBoxLayout(self.bottomFrame)
      bottomLayout.setContentsMargins(14, 6, 14, 8)
      bottomLayout.setSpacing(8)
      buttonBox = QtWidgets.QDialogButtonBox()
      self.acceptButton = buttonBox.addButton(self.tr('Accept'),
         QtWidgets.QDialogButtonBox.AcceptRole)
      self.cancelButton = buttonBox.addButton(self.tr('Cancel'),
         QtWidgets.QDialogButtonBox.RejectRole)
      self.acceptButton.setFixedWidth(100)
      self.cancelButton.setFixedWidth(100)
      bottomLayout.addStretch(1)
      bottomLayout.addWidget(buttonBox)

   def connectSignals(self):
      """Connect all signals to their handlers."""
      self.acceptButton.clicked.connect(self.accept)
      self.cancelButton.clicked.connect(self.reject)

   def accept(self):
      """Validate and save settings, then accept dialog if successful."""
      if not self.validateAllSettings():
         return
      try:
         self.saveSettings()
      except (OSError, IOError, PermissionError) as e:
         QtWidgets.QMessageBox.critical(
            self,
            self.tr('Settings Error'),
            self.tr('Failed to save settings: {}').format(str(e))
         )
         return
      super().accept()

   def showEvent(self, event):
      """Handle show event to ensure the dialog is properly displayed."""
      super().showEvent(event)
      flags = self.windowState()
      flags = flags & ~QtCore.Qt.WindowMinimized
      flags = flags | QtCore.Qt.WindowActive
      self.setWindowState(flags)
      self.raise_()

   def onBridgeReady(self):
      """Called when bridge is ready - load wallet list."""
      self.walletTab.loadWalletList()

   def registerWidgetActivateTime(self, widget):
      """Stub for entropy collection - no-op during setup."""
      pass

   def loadSettings(self):
      """Load settings for all tabs."""
      self.walletTab.loadSettings()
      self.coreTab.loadSettings()
      self.databaseTab.loadSettings()

   def validateAllSettings(self):
      """Run validators on all tabs. Returns True if all valid."""
      if not self.coreTab.validateCorePath():
         return False
      if not self.walletTab.validate():
         return False
      if not self.coreTab.validate():
         return False
      if not self.databaseTab.validateDbPath():
         return False
      if not self.databaseTab.validate():
         return False
      return True

   def _setSettingIfChanged(self, name, value):
      """Only write setting to file if the value has actually changed."""
      try:
         currentValue = TheSettings.get(name)
         if currentValue != value:
            TheSettings.set(name, value)
      except Exception:
         TheSettings.set(name, value)

   def saveSettings(self):
      """Save settings from all tabs."""
      walletSettings = self.walletTab.collectSettings()
      coreSettings = self.coreTab.collectSettings()
      dbSettings = self.databaseTab.collectSettings()

      # Paths
      self._setSettingIfChanged('CoreDataDir', coreSettings['corePath'])
      self._setSettingIfChanged('ArmoryDataDir', walletSettings['armoryPath'])
      self._setSettingIfChanged('DBDir', dbSettings['dbPath'])
      self._setSettingIfChanged('SatoshiDatadir', coreSettings['corePath'])

      # Core settings
      self._setSettingIfChanged('ManageSatoshi', coreSettings['manageSatoshi'])
      self._setSettingIfChanged('NetworkMode', coreSettings['networkMode'])

      # Database settings
      dbScenario = dbSettings['scenario']
      self._setSettingIfChanged('DBScenario', dbScenario)

      if dbScenario == SCENARIO_DB_LOCAL:
         dbTypeVal = 'DB_SUPER' if dbSettings['typeDisp'] == 'Supernode' \
            else 'DB_FULL'
         self._setSettingIfChanged('DBType', dbTypeVal)
         if dbSettings['ram']:
            self._setSettingIfChanged('RAMUsage', int(dbSettings['ram']))
         if dbSettings['threads']:
            self._setSettingIfChanged('ThreadCount', int(dbSettings['threads']))
      elif dbScenario == SCENARIO_DB_REMOTE:
         if dbSettings['remoteHost']:
            self._setSettingIfChanged('RemoteDBHost', dbSettings['remoteHost'])
         if dbSettings['remotePort']:
            self._setSettingIfChanged('RemoteDBPort', dbSettings['remotePort'])

      if self.main:
         self.main.setSatoshiPaths()
