################################################################################
#                                                                              #
#  Copyright (C) 2026, goatpig                                                 #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

import os

from qtpy import QtCore, QtWidgets
from armoryengine.ArmoryUtils import CLI_OPTIONS, \
   ARMORY_HOME_DIR, ARMORYDB_DEFAULT_PORT, \
   LOGINFO, LOGERROR
from armoryengine.Settings import TheSettings
from armoryengine.CppBridge import TheBridge, ServerPush, \
   PeersDbCallback, ServerKeyCallback
from ui.QtExecuteSignal import TheSignalExecution

from qtdialogs.ArmoryDialog import ArmoryDialog
import qtdialogs.qtdefines as qtdefines

from qtdialogs.setupmanager.WalletTab import WalletTab
from qtdialogs.setupmanager.CoreTab import CoreTab
from qtdialogs.setupmanager.DatabaseTab import (
   DatabaseTab, SCENARIO_DB_AUTOMATED, SCENARIO_DB_OFFLINE,
   SCENARIO_REMOTE_IP, SCENARIO_REMOTE_PEER
)

# Dialog-specific constants (only used by this dialog)
MINIMUM_DIALOG_WIDTH = 500
MINIMUM_DIALOG_HEIGHT = 640

################################################################################
class DlgSetupManager(ArmoryDialog):
   """
   Setup Manager Dialog for Armory configuration.

   Orchestrates the three settings tabs:
   - Wallet settings and migration
   - Bitcoin Core configuration
   - Database settings (local/remote/none)
   """
   _ipConnectDone = QtCore.Signal()

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
      self._connectionSuccess = False

      # Callback state tracking for async operations
      self.peersDbCallback = None
      self.serverKeyCallback = None
      self.pendingConnectionResult = None

      self.setupDialogProperties()
      self.initTabs()
      self.setupMainLayout()
      self.loadSettings()
      self.connectSignals()
      self._handleCoreTabActivation()

      # Peers DB auto-load happens in onBridgeReady
      # (bridge isn't available during __init__)

   @property
   def connectionSuccess(self):
      return self._connectionSuccess

   def setupDialogProperties(self):
      """Configure basic dialog properties and styling."""
      self.setMinimumWidth(MINIMUM_DIALOG_WIDTH)
      self.setMinimumHeight(MINIMUM_DIALOG_HEIGHT)
      self.setWindowTitle(self.tr('Armory Setup Manager'))
      self.setWindowFlags(QtCore.Qt.Window)
      self.setModal(True)
      self.setSizePolicy(QtWidgets.QSizePolicy.Preferred,
         QtWidgets.QSizePolicy.Preferred)
      qtdefines.applyDialogBaseStyle(self)

   def initTabs(self):
      """Initialize all tab widgets."""
      self.tabWidget = QtWidgets.QTabWidget()
      self.tabWidget.setContentsMargins(14, 6, 14, 8)
      self.walletTab = WalletTab(self, self.main)
      self.coreTab = CoreTab(self, self.main)
      self.databaseTab = DatabaseTab(self, self.main)

      self.tabWidget.addTab(self.walletTab, self.tr('Wallet Settings'))
      self.tabWidget.addTab(self.databaseTab, self.tr('Database Settings'))
      self.tabWidget.addTab(self.coreTab, self.tr('Core Settings'))

      if CLI_OPTIONS.offline:
         dbTabIndex = self.tabWidget.indexOf(self.databaseTab)
         self.tabWidget.setTabEnabled(dbTabIndex, False)
         self.tabWidget.setTabText(
            dbTabIndex, self.tr('Database Settings, Offline'))
         coreTabIndex = self.tabWidget.indexOf(self.coreTab)
         self.tabWidget.setTabEnabled(coreTabIndex, False)

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

      self.databaseTab.modeGroup.idToggled.connect(
         self.onDbScenarioChanged)
      self.databaseTab.remoteSubModeCombo \
         .currentIndexChanged.connect(
            self.onRemoteSubModeChanged)

      self.databaseTab.testConnectionRequested.connect(
         self.onTestConnectionRequested)

   def accept(self):
      """
      Validate settings, ensure valid connection, save, and accept.

      Connection validation rules (per maintainer requirements):
      - Offline mode: always allowed
      - Connection already tested and succeeded: use existing live connection
      - No test but valid settings: attempt connection now
      - No connection possible: force user to Database tab
      """
      if not self.validateAllSettings():
         return

      # Get current scenario
      dbSettings = self.databaseTab.collectSettings()
      scenario = dbSettings['scenario']

      # Offline mode is always valid - no connection needed
      if scenario == SCENARIO_DB_OFFLINE:
         LOGINFO("Offline mode - no connection validation needed")
         #TODO: set instance wide offline flag
         self._saveAndAccept()
         return

      if self.connectionSuccess:
         LOGINFO("Using existing tested connection")
         self._saveAndAccept()
         return

      # No explicit test - try to connect now
      LOGINFO("No tested connection - attempting "
         "connection on accept")
      self.setCursor(QtCore.Qt.WaitCursor)
      QtWidgets.QApplication.processEvents()

      params = self.getDbConnectionParams()
      self.initiateDbConnection(params)

   def _handleConnectionAttemptFinality(self, success, error):
      self.unsetCursor()

      if success:
         self._connectionSuccess = True
         LOGINFO("Connection established on accept")
         self._saveAndAccept()
         return

      # Connection failed - force user to Database tab
      errorMsg = error if error else self.tr('Unknown error')
      QtWidgets.QMessageBox.warning(
         self,
         self.tr('Connection Required'),
         self.tr('Cannot save settings without a valid database connection.\n\n'
                 'Error: {}\n\n'
                 'Please configure a valid connection or select Offline mode.'
                 ).format(errorMsg)
      )
      self.tabWidget.setCurrentWidget(self.databaseTab)
      # Don't call super().accept() - dialog stays open

   def _saveAndAccept(self):
      """Save settings and accept the dialog."""
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
      """Called when bridge is ready."""
      self.walletTab.loadWalletList()
      self._autoLoadPeersIfNeeded()
      self.coreTab.onBridgeReady()

   def registerWidgetActivateTime(self, widget):
      """Stub for entropy collection - no-op during setup."""
      pass

   def loadSettings(self):
      """Load settings for all tabs."""
      self.walletTab.loadSettings()
      self.coreTab.loadSettings()
      self.databaseTab.loadSettings()

   def onDbScenarioChanged(self, radioId, checked):
      """Handle database mode radio changes."""
      if not checked:
         return
      self._autoLoadPeersIfNeeded()
      self._handleCoreTabActivation()

   def onRemoteSubModeChanged(self, index):
      """Handle remote sub-mode combo changes.

      Auto-load peers DB when switching to Peer mode.
      Falls back to IP if load fails or user cancels.
      """
      self._autoLoadPeersIfNeeded()

   def _autoLoadPeersIfNeeded(self):
      """Load peers DB if in Peer mode and not loaded.

      If the file exists on disk, load it directly (C++ will
      prompt for passphrase if encrypted).

      If the file does NOT exist, ask the user whether to
      create one before invoking C++. This is necessary
      because once C++ enters the PeerFileMissing path, it
      always creates the file -- there is no abort.
      """
      scenario = self.databaseTab.getScenario()
      if scenario != SCENARIO_REMOTE_PEER:
         return
      if self.databaseTab.peersDbLoaded:
         return

      peersPath = os.path.join(
         ARMORY_HOME_DIR, 'client.peers')
      if os.path.exists(peersPath):
         self.loadPeersDatabase()
         return

      # File missing -- ask before calling C++
      reply = QtWidgets.QMessageBox.question(
         self,
         self.tr('Create Peers Database'),
         self.tr(
            'No peers database found.\n\n'
            'Peer mode requires a peers database '
            'to store authorized keys.\n\n'
            'Create one now?'),
         QtWidgets.QMessageBox.Yes
            | QtWidgets.QMessageBox.No,
         QtWidgets.QMessageBox.Yes)

      if reply == QtWidgets.QMessageBox.Yes:
         self.loadPeersDatabase()
      else:
         LOGINFO("User declined peers DB creation"
            " - switching to IP mode")
         self.databaseTab.remoteSubModeCombo \
            .setCurrentIndex(0)

   def _handleCoreTabActivation(self):
      scenario = self.databaseTab.getScenario()
      coreTabIndex = self.tabWidget.indexOf(self.coreTab)
      if scenario == SCENARIO_DB_AUTOMATED:
         #core automation only relevant if db is automated too
         self.tabWidget.setTabEnabled(coreTabIndex, True)
      else:
         self.tabWidget.setTabEnabled(coreTabIndex, False)

   def onTestConnectionRequested(self):
      """
      Handle test connection request from DatabaseTab.

      Attempts to connect using current settings. A successful
      test establishes a live connection - no need to reconnect
      on dialog accept. C++ bug: cleanupDb does not reset
      bdvPtr_, so reconnection on the same bridge is not
      possible. After a successful test, settings are locked.
      """
      LOGINFO("Test connection requested")

      if self.connectionSuccess:
         QtWidgets.QMessageBox.information(
            self,
            self.tr('Already Connected'),
            self.tr('Connection already established. '
               'Accept to continue with current settings '
               'or Cancel to restart the setup.'))
         return

      # Validate settings first
      if not self.databaseTab.validate():
         return

      # Get connection params and attempt connection
      params = self.getDbConnectionParams()
      scenario = params['scenario']

      if scenario == SCENARIO_DB_OFFLINE:
         QtWidgets.QMessageBox.information(
            self,
            self.tr('Offline Mode'),
            self.tr('Offline mode selected - no connection to test.')
         )
         return

      btn = self.databaseTab.testConnectionButton
      savedLabel = btn.text()
      btn.setEnabled(False)
      btn.setText(self.tr("Connecting..."))
      QtWidgets.QApplication.processEvents()

      success, error = self.initiateDbConnection(params)
      if success:
         self._connectionSuccess = True
         self.databaseTab.setDbSettingsLocked(True)
         QtWidgets.QMessageBox.information(
            self,
            self.tr('Connection Successful'),
            self.tr(
               'Successfully connected to ArmoryDB.'))
         return

      btn.setEnabled(True)
      btn.setText(savedLabel)
      errorMsg = error if error \
         else self.tr('Unknown error')
      QtWidgets.QMessageBox.warning(
         self,
         self.tr('Connection Failed'),
         self.tr(
            'Failed to connect:\n\n{}'
            ).format(errorMsg))

   def validateAllSettings(self):
      """Run validators on all tabs. Returns True if all valid."""
      if not self.walletTab.validate():
         return False

      coreTabIndex = self.tabWidget.indexOf(self.coreTab)
      if self.tabWidget.isTabEnabled(coreTabIndex):
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
      except KeyError:
         TheSettings.set(name, value)

   def saveSettings(self):
      """Save settings from all tabs."""
      walletSettings = self.walletTab.collectSettings()
      coreSettings = self.coreTab.collectSettings()
      dbSettings = self.databaseTab.collectSettings()

      # Paths
      self._setSettingIfChanged('ArmoryDataDir', walletSettings['armoryPath'])
      self._setSettingIfChanged('DBDir', dbSettings['dbPath'])
      self._setSettingIfChanged('SatoshiDatadir', coreSettings['datadir'])
      self._setSettingIfChanged('SatoshiBin', coreSettings['binpath'])

      # Core settings
      self._setSettingIfChanged('ManageSatoshi', coreSettings['automate'])
      if coreSettings['p2pPort']:
         self._setSettingIfChanged('BitcoinP2PPort', coreSettings['p2pPort'])
      if coreSettings['rpcPort']:
         self._setSettingIfChanged('BitcoinRPCPort', coreSettings['rpcPort'])

      # Database settings
      dbScenario = dbSettings['scenario']
      self._setSettingIfChanged(
         'DBScenario', dbScenario)
      if dbSettings.get('setAsDefault'):
         self._setSettingIfChanged(
            'DBScenarioDefault', dbScenario)

      if dbScenario == SCENARIO_DB_AUTOMATED:
         dbTypeVal = 'DB_SUPER' \
            if dbSettings['typeDisp'] == 'Supernode' \
            else 'DB_BARE'
         self._setSettingIfChanged(
            'DBType', dbTypeVal)
         if dbSettings['ram']:
            self._setSettingIfChanged(
               'RAMUsage', int(dbSettings['ram']))
         if dbSettings['threads']:
            self._setSettingIfChanged(
               'ThreadCount',
               int(dbSettings['threads']))
      elif dbScenario == SCENARIO_REMOTE_IP:
         self._setSettingIfChanged(
            'RemoteIpAddr', dbSettings['ipAddr'])
         self._setSettingIfChanged(
            'RemoteIpPort', dbSettings['ipPort'])
      elif dbScenario == SCENARIO_REMOTE_PEER:
         self._setSettingIfChanged(
            'RemotePeerKey', dbSettings['peerKey'])

      if self.main:
         self.main.setSatoshiPaths()

   def getDbConnectionParams(self):
      """Get database connection parameters from UI."""
      dbSettings = self.databaseTab.collectSettings()
      coreSettings = self.coreTab.collectSettings()
      scenario = SCENARIO_DB_OFFLINE if CLI_OPTIONS.offline == True \
         else dbSettings['scenario']
      params = {'scenario': scenario}

      if scenario == SCENARIO_DB_AUTOMATED:
         params['satoshiPath'] = coreSettings['datadir']
         params['satoshiBin'] = coreSettings['binpath']
         params['dbPath'] = dbSettings['dbPath']
         params['automateNode'] = coreSettings['automate']
      elif scenario == SCENARIO_REMOTE_PEER:
         params['peerKey'] = dbSettings['peerKey']
      elif scenario == SCENARIO_REMOTE_IP:
         params['ipAddr'] = dbSettings['ipAddr']
         params['ipPort'] = dbSettings['ipPort'] \
            if dbSettings['ipPort'] \
            else str(ARMORYDB_DEFAULT_PORT)

      return params

   def initiateDbConnection(self, params=None):
      """
      Initiate database connection based on settings.

      Args:
         params: Connection parameters dict (from getDbConnectionParams).
                 If None, will collect from current UI state.

      Returns:
         tuple: (success: bool, error: str or None)
      """
      if params is None:
         params = self.getDbConnectionParams()

      scenario = params['scenario']
      LOGINFO(f"Initiating DB connection: scenario={scenario}")

      if scenario == SCENARIO_DB_OFFLINE:
         LOGINFO("Offline mode - no database connection")
         return (True, None)

      elif scenario == SCENARIO_DB_AUTOMATED:
         return self._connectLocalDb(params)
      elif scenario == SCENARIO_REMOTE_PEER:
         success, error = self._connectToPeer(params)
         self._handleConnectionAttemptFinality(success, error)
      elif scenario == SCENARIO_REMOTE_IP:
         success, error = self._connectToIp(params)
         self._handleConnectionAttemptFinality(success, error)

      raise ValueError(f"Unknown scenario: {scenario}")

   def _handleAutomationReply(self, reply):
      TheSignalExecution.executeMethod(
         self._handleConnectionAttemptFinality,
         reply.success, reply.error
      )

   def _connectLocalDb(self, params):
      """Initiate local (automated) database connection."""
      print (f"params: {params}")
      satoshiDir = params.get('satoshiPath', '')
      satoshiBin = params.get('satoshiBin', '')
      dbDir = params.get('dbPath', '')
      automateNode = params.get('automateNode', False)

      LOGINFO("automating db/node operations")
      result = TheBridge.dbSetup.initAutomationContext(
         satoshiDir=satoshiDir, satoshiBin=satoshiBin,
         dbDir=dbDir, automateNode=automateNode, automateDb=True)

      if result.success == False:
         LOGINFO("automation context init failed!")
         return (False, result.error)

      dlgAutomations = DlgAutomations(self, self.main, automateNode)
      TheBridge.dbSetup.runAutomationContext(
         dlgAutomations.callbackId,
         self._handleAutomationReply)
      dlgAutomations.exec_()

   def _connectToPeer(self, params):
      """Connect to a known peer from the peers database."""
      peerKey = params.get('peerKey', '')
      if not peerKey:
         raise ValueError(
            "peerKey missing from params")

      LOGINFO(f"Calling connectToPeer: "
         f"key={peerKey[:20]}...")

      result = TheBridge.dbSetup.connectToPeer(
         peerKey)

      if result.success:
         LOGINFO("connectToPeer succeeded")
         return (True, None)

      LOGERROR("connectToPeer failed: "
         f"{result.error}")
      return (False, result.error)

   def _connectToIp(self, params):
      """Connect to a remote database by IP address (1-way auth).

      Async flow:
      1. connectToIp is called with a result callback
      2. Server presents its public key via notification callback
      3. User approves/rejects the key (on Qt thread)
      4. C++ finishes connecting (or fails)
      5. Result callback fires with actual success/failure
      """
      ipAddr = params.get('ipAddr', '')
      ipPort = params.get(
         'ipPort', str(ARMORYDB_DEFAULT_PORT))

      if not ipAddr:
         raise ValueError(
            "ipAddr missing from params")

      LOGINFO(f"Calling connectToIp: {ipAddr}:{ipPort}")

      callbackId = f"connectToIp_{ipAddr}_{ipPort}"
      self.pendingConnectionResult = None

      def onConnectResult(reply):
         if reply.success:
            self.pendingConnectionResult = (True, None)
         else:
            err = reply.error if reply.error \
               else "Connection failed"
            self.pendingConnectionResult = (False, err)
         self._ipConnectDone.emit()

      self.serverKeyCallback = ServerKeyCallback(
         callbackId=callbackId,
         onPresentPubkey=self._onServerKeyPresented)

      TheBridge.dbSetup.connectToIp(
         ip=ipAddr, port=ipPort,
         callbackId=callbackId,
         resultCallback=onConnectResult)

      LOGINFO("connectToIp initiated, waiting for key")

      loop = QtCore.QEventLoop()
      timer = QtCore.QTimer()
      timer.setSingleShot(True)
      timer.timeout.connect(loop.quit)
      self._ipConnectDone.connect(loop.quit)

      try:
         timer.start(30000)
         loop.exec_()
      finally:
         timer.stop()
         self._ipConnectDone.disconnect(loop.quit)

      if self.pendingConnectionResult is None:
         return (False, "Connection timeout")

      return self.pendingConnectionResult

   def _onServerKeyPresented(self, callback, serverPubkey):
      """Show server key for approval (marshaled to Qt thread)."""
      LOGINFO(f"Server key presented: {serverPubkey[:20]}...")

      def promptOnQtThread():
         msg = self.tr(
            "The server is presenting its "
            "public key for verification."
            "\n\nServer Public Key:\n{}"
            "\n\nDo you want to accept this "
            "connection?\n\n"
            "Note: Only accept if you trust "
            "this server."
         ).format(serverPubkey)

         reply = QtWidgets.QMessageBox.question(
            self,
            self.tr('Verify Server Key'),
            msg,
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No,
            QtWidgets.QMessageBox.No
         )
         if reply == QtWidgets.QMessageBox.Yes:
            LOGINFO("User accepted server key")
            callback.replyAck(accept=True)
         else:
            LOGINFO("User rejected server key")
            callback.replyAck(accept=False)

      TheSignalExecution.executeMethod(promptOnQtThread)

   def loadPeersDatabase(self):
      """Load the peers database via bridge (non-blocking).

      Non-blocking so the Qt thread stays responsive for
      passphrase prompts that fire during the load.
      If already loaded, just refreshes the peer list.
      """
      if self.databaseTab.peersDbLoaded:
         self.databaseTab.refreshPeerList()
         LOGINFO("Peers DB already loaded, refreshed")
         return True

      LOGINFO("Loading peers database...")
      callbackId = "loadPeersDb_setup"

      self.peersDbCallback = PeersDbCallback(
         callbackId=callbackId,
         onUnlockRequest=\
            self._onPeersDbUnlockRequest,
         onSetPassphrase=\
            self._onPeersDbSetPassphrase,
         onSuccess=None)

      def onLoadResult(reply):
         if reply.success:
            LOGINFO("Peers database loaded")
            TheSignalExecution.executeMethod(
               self.databaseTab
                  .setPeersDbLoaded, True)
         else:
            LOGERROR(
               "loadPeersDb failed: "
               f"{reply.error}")
            TheSignalExecution.executeMethod(
               self.databaseTab
                  .remoteSubModeCombo
                  .setCurrentIndex, 0)

      TheBridge.dbSetup.loadPeersDb(
         callbackId,
         resultCallback=onLoadResult)
      return True

   def _onPeersDbUnlockRequest(self, callback, unlockRequest):
      """Prompt for passphrase (marshaled to Qt thread)."""
      LOGINFO("Peers DB unlock request received")

      def promptOnQtThread():
         passphrase, ok = QtWidgets.QInputDialog.getText(
            self,
            self.tr('Unlock Peers Database'),
            self.tr('Enter passphrase to unlock the peers database:'),
            QtWidgets.QLineEdit.Password
         )
         if ok and passphrase:
            callback.replyUnlock(passphrase, success=True)
         else:
            callback.replyUnlock('', success=False)

      TheSignalExecution.executeMethod(promptOnQtThread)

   def _onPeersDbSetPassphrase(self, callback, passphraseRequest):
      """Prompt for new passphrase (marshaled to Qt).

      Two choices:
      - Set passphrase: encrypted peers DB
      - Skip: unencrypted DB (loads silently next run)

      No Cancel here -- user already confirmed creation in
      _autoLoadPeersIfNeeded. C++ cannot abort once it
      enters the PeerFileMissing path.
      """
      LOGINFO("Peers DB set passphrase request")

      def promptOnQtThread():
         msg = QtWidgets.QMessageBox(self)
         msg.setWindowTitle(
            self.tr('Encrypt Peers Database'))
         msg.setText(self.tr(
            'Set a passphrase to encrypt the peers '
            'database, or skip to leave it '
            'unencrypted.'))
         setBtn = msg.addButton(
            self.tr('Set Passphrase'),
            QtWidgets.QMessageBox.AcceptRole)
         skipBtn = msg.addButton(
            self.tr('Skip (no encryption)'),
            QtWidgets.QMessageBox.ActionRole)
         msg.setDefaultButton(setBtn)
         msg.exec_()

         clicked = msg.clickedButton()
         if clicked == skipBtn:
            LOGINFO("Creating unencrypted peers DB")
            callback.replySetPassphrase(
               '', success=False)
            return

         # Set passphrase flow
         passphrase, ok = \
            QtWidgets.QInputDialog.getText(
               self,
               self.tr('Set Passphrase'),
               self.tr('Enter passphrase for the '
                  'peers database:'),
               QtWidgets.QLineEdit.Password)
         if not ok or not passphrase:
            callback.replySetPassphrase(
               '', success=False)
            return

         confirm, ok = \
            QtWidgets.QInputDialog.getText(
               self,
               self.tr('Confirm Passphrase'),
               self.tr('Confirm the passphrase:'),
               QtWidgets.QLineEdit.Password)
         if not ok or confirm != passphrase:
            QtWidgets.QMessageBox.warning(
               self,
               self.tr('Mismatch'),
               self.tr('Passphrases did not match. '
                  'Database will be created '
                  'without encryption.'))
            callback.replySetPassphrase(
               '', success=False)
            return

         callback.replySetPassphrase(
            passphrase, kdfTargetMs=250)

      TheSignalExecution.executeMethod(
         promptOnQtThread)

################################################################################
class DlgAutomations(ArmoryDialog, ServerPush):
   """
   Modal to render automation notifications.
   See runAutomationContext and cleanup in class DbSetupService
   """
   def __init__(self, parent, main, withNode: bool):
      ArmoryDialog.__init__(self, parent, main)
      ServerPush.__init__(self)

      #setup modal
      layout = QtWidgets.QVBoxLayout()
      self.progressDesc = "Progressing..."
      self.progressLabel = QtWidgets.QLabel(self.progressDesc)
      layout.addWidget(self.progressLabel)
      self.setLayout(layout)

      if withNode:
         self.setWindowTitle("Automating Bitcoin Core and ArmoryDB")
      else:
         self.setWindowTitle("Automating ArmoryDB")

   def updateLabel(self, descLine):
      self.progressDesc += f"\n - {descLine}"
      self.progressLabel.setText(self.progressDesc)
      self.setVisible(True)

   def parseProtoPacket(self, protoPacket):
      """override of ServerPush method"""
      which = protoPacket.which()
      if which == "cleanup":
         TheSignalExecution.executeMethod(self.reject)

      elif which == "automation":
         TheSignalExecution.executeMethod(
            self.updateLabel, protoPacket.automation.which())
