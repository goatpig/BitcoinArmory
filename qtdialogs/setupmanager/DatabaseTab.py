################################################################################
#                                                                              #
#  Copyright (C) 2026, goatpig                                                 #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

import os

from qtpy import QtCore, QtWidgets, QtGui
from armoryengine.ArmoryUtils import ARMORY_DB_DIR, ARMORY_HOME_DIR, \
   BTC_HOME_DIR, ARMORYDB_DEFAULT_PORT, LOGINFO
from armoryengine.Settings import TheSettings
from armoryengine.CppBridge import TheBridge
from armorycolors import htmlColor

import qtdialogs.qtdefines as qtdefines

# Database scenario constants
SCENARIO_DB_LOCAL = "Automate ArmoryDB"
SCENARIO_REMOTE_IP = "Connect to IP"
SCENARIO_REMOTE_PEER = "Connect to Peer"
SCENARIO_DB_NONE = "Offline"

def isRemoteScenario(scenario):
   return scenario in (SCENARIO_REMOTE_IP, SCENARIO_REMOTE_PEER)

# Validation limits
MAX_RAM_USAGE = 256      # Max RAM in 128MB increments (~32GB)
MAX_THREAD_COUNT = 64    # Max threads for DB operations

################################################################################
class PeerData:
   """
   Data class for remote database peer information.

   Matches bridge PeerData/Peer structure:
   - key: human-readable peer key (AR1.../AR2.../ARc...)
   - names: list of ip:port strings
   - label: human-readable label
   - oneWay: True for 1-way auth, False for 2-way
   """
   def __init__(self,
      key='', names=None, label='', oneWay=True):
      self.key = key
      self.names = names if names else []
      self.label = label
      self.oneWay = oneWay

   @classmethod
   def fromBridgePeer(cls, bridgePeerData):
      """Create PeerData from bridge listPeers entry."""
      peer = bridgePeerData.peer
      return cls(
         key=peer.key,
         names=list(peer.names),
         label=peer.label,
         oneWay=bridgePeerData.oneWay
      )

   def isOwn(self):
      return 'own' in self.names

   def authModeStr(self):
      return '1-way' if self.oneWay else '2-way'

################################################################################
class AddPeerDialog(QtWidgets.QDialog):
   """Dialog for adding a new peer to the peers database."""
   def __init__(self, parent):
      super().__init__(parent)
      self.peerKey = ''
      self.address = ''
      self.peerLabel = ''
      self.setWindowTitle(self.tr('Add Peer'))
      self.setMinimumWidth(450)
      self.initUI()

   def initUI(self):
      layout = QtWidgets.QVBoxLayout(self)
      layout.setSpacing(12)

      formLayout = QtWidgets.QGridLayout()
      formLayout.setSpacing(8)

      keyLabel = QtWidgets.QLabel(self.tr("Peer Key:"))
      self.keyEdit = QtWidgets.QLineEdit()
      self.keyEdit.setPlaceholderText(
         "AR1... or AR2... server peer key")
      formLayout.addWidget(keyLabel, 0, 0)
      formLayout.addWidget(self.keyEdit, 0, 1)

      keyInfo = QtWidgets.QLabel(
         self.tr("Get this from the remote "
            "ArmoryDB startup output."))
      keyInfo.setStyleSheet(
         f"color: {htmlColor('DisableFG')}; "
         "font-style: italic; font-size: 9pt;")
      formLayout.addWidget(keyInfo, 1, 1)

      addressLabel = QtWidgets.QLabel(
         self.tr("Address, ip:port:"))
      self.addressEdit = QtWidgets.QLineEdit()
      self.addressEdit.setPlaceholderText(
         f"e.g. 127.0.0.1:{ARMORYDB_DEFAULT_PORT}")
      formLayout.addWidget(addressLabel, 2, 0)
      formLayout.addWidget(self.addressEdit, 2, 1)

      addrInfo = QtWidgets.QLabel(
         self.tr("IP address and port only. "
            "Domains not supported yet."))
      addrInfo.setStyleSheet(
         f"color: {htmlColor('DisableFG')}; "
         "font-style: italic; font-size: 9pt;")
      formLayout.addWidget(addrInfo, 3, 1)

      labelLabel = QtWidgets.QLabel(
         self.tr("Label:"))
      self.labelEdit = QtWidgets.QLineEdit()
      self.labelEdit.setPlaceholderText(
         self.tr("optional, e.g. Home Server"))
      formLayout.addWidget(labelLabel, 4, 0)
      formLayout.addWidget(self.labelEdit, 4, 1)

      formLayout.setColumnStretch(1, 1)
      layout.addLayout(formLayout)

      authTipText = self.tr(
         '<b>AR1 keys.</b> 1-way auth: client '
         'verifies the server.'
         '<br><br>'
         '<b>AR2 keys.</b> 2-way auth: both sides '
         'verify each other. Server needs to know '
         'of the client ARc key beforehand.')
      authTip = qtdefines.createInstantToolTip(
         authTipText)

      buttonBox = QtWidgets.QDialogButtonBox(
         QtWidgets.QDialogButtonBox.Ok
            | QtWidgets.QDialogButtonBox.Cancel)
      buttonBox.accepted.connect(self.validateAndAccept)
      buttonBox.rejected.connect(self.reject)

      btnRow = QtWidgets.QHBoxLayout()
      btnRow.addWidget(authTip)
      btnRow.addStretch()
      btnRow.addWidget(buttonBox)
      layout.addLayout(btnRow)

   def validateAndAccept(self):
      peerKey = self.keyEdit.text().strip()
      if not peerKey:
         QtWidgets.QMessageBox.warning(
            self, self.tr('Missing Peer Key'),
            self.tr('Peer key is required to add a peer.'))
         return

      address = self.addressEdit.text().strip()
      if not address or ':' not in address:
         QtWidgets.QMessageBox.warning(
            self, self.tr('Invalid Address'),
            self.tr('Please enter address as ip:port.'))
         return

      self.peerKey = peerKey
      self.address = address
      self.peerLabel = self.labelEdit.text().strip()
      self.accept()

################################################################################
class DatabaseTab(QtWidgets.QWidget):
   """
   Database settings tab for Setup Manager.

   Manages:
   - Database scenario via radio buttons (automate/connect/offline)
   - ArmoryDB configuration (type, RAM, threads)
   - Remote database configuration:
     - Connect to known peer (2-way auth via peers DB)
     - Connect by IP (1-way auth, ad-hoc)
   - CLI command display (informational)

   Signals:
      testConnectionRequested: Emitted when user clicks Connect.
         Parent (DlgSetupManager) handles the actual attempt.
   """
   testConnectionRequested = QtCore.Signal()

   def __init__(self, parent, main=None):
      super().__init__(parent)
      self.main = main

      # Radio buttons and sections
      self.autoDbRadio = None
      self.connectRadio = None
      self.offlineRadio = None
      self.modeGroup = None
      self.autoDbSection = None
      self.connectSection = None

      # Common widgets
      self.databaseDirEdit = None
      self.cliCommandLabel = None
      self.dbBootstrapLabel = None

      # Local (automate) mode widgets
      self.localDatabaseFrame = None
      self.databaseTypeCombo = None
      self.ramUsageEdit = None
      self.threadCountEdit = None

      # Remote mode widgets
      self.remoteSubModeCombo = None
      self.peerFrame = None
      self.ipFrame = None
      self.ownKeyFrame = None

      # Remote Peer sub-mode widgets
      self.peerList = None
      self.addPeerButton = None
      self.removePeerButton = None
      self.editLabelButton = None

      # Remote IP sub-mode widgets
      self.ipEdit = None
      self.portEdit = None
      self.ownKeyEdit = None

      # Test connection widgets
      self.testConnectionButton = None

      # Default scenario and checkbox
      self.setDefaultCheckbox = None
      self.defaultHintLabel = None
      self._savedDefaultScenario = SCENARIO_DB_LOCAL

      # State tracking
      self.peersDbLoaded = False
      self.ownKey = ''

      self.initUI()

   def initUI(self):
      """Initialize the database settings tab UI."""
      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.setContentsMargins(14, 6, 14, 8)
      mainLayout.setSpacing(8)

      title = qtdefines.createTabTitle(
         self.tr('Database Settings'))
      mainLayout.addWidget(title)

      self.initWidgets()

      # Mode selection group
      self.modeGroup = QtWidgets.QButtonGroup(self)
      self.autoDbRadio = QtWidgets.QRadioButton(
         self.tr('Automate ArmoryDB'))
      self.connectRadio = QtWidgets.QRadioButton(
         self.tr('Connect to remote'))
      self.offlineRadio = QtWidgets.QRadioButton(
         self.tr('Offline'))
      self.modeGroup.addButton(self.autoDbRadio, 0)
      self.modeGroup.addButton(self.connectRadio, 1)
      self.modeGroup.addButton(self.offlineRadio, 2)
      self.autoDbRadio.setChecked(True)

      autoDbTipText = self.tr(
         '<b>Automate ArmoryDB.</b> Runs ArmoryDB '
         'locally as a managed process. Enforces '
         '2-way auth with ad-hoc keys. The database '
         'shuts down when the client exits.'
         '<br><br>'
         '<b>Full Database.</b> Tracks only transactions '
         'relevant to your registered wallets. Lower '
         'disk and memory usage.'
         '<br><br>'
         '<b>Super Node.</b> Indexes every transaction '
         'on the blockchain. Allows lookups for any '
         'address. Requires significantly more disk '
         'space and RAM.')
      autoDbTip = qtdefines.createInstantToolTip(
         autoDbTipText)

      self.buildAutoDbSection()
      autoDbBox, self.autoDbRow = \
         qtdefines.makeRadioGroupBox(
            self.autoDbRadio, autoDbTip,
            self.autoDbSection)
      mainLayout.addWidget(autoDbBox)

      connectTipText = self.tr(
         '<b>Connect to IP.</b> Connect to a remote '
         'ArmoryDB by IP:port. 1-way auth only: you '
         'verify the server, the server does not '
         'verify you.'
         '<br><br>'
         '<b>Connect to Peer.</b> Connect to a saved '
         'peer from the peers database. Supports '
         '1-way or 2-way auth depending on the key '
         'prefix.'
         '<br><br>'
         '<b>1-way (AR1 keys).</b> Client authenticates '
         'the server. Server does not verify the '
         'client.'
         '<br><br>'
         '<b>2-way (AR2 keys).</b> Mutual authentication. '
         'Both sides verify each other. Server needs '
         'to know of the client ARc key beforehand.')
      connectTip = qtdefines.createInstantToolTip(
         connectTipText)

      self.buildConnectSection()
      connectBox, self.connectRow = \
         qtdefines.makeRadioGroupBox(
            self.connectRadio, connectTip,
            self.connectSection)
      mainLayout.addWidget(connectBox)

      offlineBox, self.offlineRow = \
         qtdefines.makeRadioGroupBox(
            self.offlineRadio)
      mainLayout.addWidget(offlineBox)

      self.defaultHintLabel = QtWidgets.QLabel(
         self.tr('(default)'))
      font = self.defaultHintLabel.font()
      font.setItalic(True)
      font.setPointSize(font.pointSize() - 1)
      self.defaultHintLabel.setFont(font)
      self.defaultHintLabel.hide()

      self.setDefaultCheckbox = QtWidgets.QCheckBox(
         self.tr('Set as default'))
      self.setDefaultCheckbox.setToolTip(
         self.tr('Remember this mode for next launch'))
      mainLayout.addWidget(self.setDefaultCheckbox)
      mainLayout.addStretch()

      # Initial state: show autoDb, hide connect
      self.connectSection.hide()
      self.modeGroup.idToggled.connect(self.onModeChanged)
      self.updateCliCommandDisplay()

      self.setLayout(mainLayout)

   def initWidgets(self):
      """Initialize database tab widgets."""
      self.databaseDirEdit = QtWidgets.QLineEdit()
      self.databaseDirEdit.setMinimumWidth(400)
      self.databaseDirEdit.textChanged.connect(
         self.updateCliCommandDisplay)

      self.databaseTypeCombo = QtWidgets.QComboBox()
      self.databaseTypeCombo.setFixedWidth(200)
      self.databaseTypeCombo.addItems(
         ["Full Database", "Supernode"])
      self.databaseTypeCombo.currentTextChanged.connect(
         self.updateCliCommandDisplay)

      self.ramUsageEdit = QtWidgets.QLineEdit()
      self.ramUsageEdit.setFixedWidth(100)
      self.ramUsageEdit.textChanged.connect(
         self.updateCliCommandDisplay)

      self.threadCountEdit = QtWidgets.QLineEdit()
      self.threadCountEdit.setFixedWidth(100)
      self.threadCountEdit.textChanged.connect(
         self.updateCliCommandDisplay)

      # Remote sub-mode combo (IP vs Peer)
      self.remoteSubModeCombo = QtWidgets.QComboBox()
      self.remoteSubModeCombo.setFixedWidth(200)
      self.remoteSubModeCombo.addItems(
         ["Connect to IP", "Connect to Peer"])
      self.remoteSubModeCombo.currentIndexChanged.connect(
         self.onRemoteSubModeChanged)

      # Remote Peer sub-mode widgets
      self.peerList = QtWidgets.QListWidget()
      self.peerList.setMinimumWidth(200)
      self.peerList.setFixedHeight(100)
      self.peerList.setSelectionMode(
         QtWidgets.QAbstractItemView.SingleSelection)
      self.peerList.setAlternatingRowColors(False)
      self._showPeerListHint(
         self.tr("No peers yet, use Add Peer to add one"))

      peerBtnWidth = qtdefines.relaxedSizeNChar(
         self, 10)[0]
      self.addPeerButton = QtWidgets.QPushButton(
         self.tr("Add"))
      self.addPeerButton.setFixedWidth(peerBtnWidth)
      self.addPeerButton.clicked.connect(
         self.addPeerFromTab)
      self.addPeerButton.setEnabled(False)

      self.removePeerButton = QtWidgets.QPushButton(
         self.tr("Remove"))
      self.removePeerButton.setFixedWidth(peerBtnWidth)
      self.removePeerButton.clicked.connect(
         self.removePeerFromTab)
      self.removePeerButton.setEnabled(False)
      self.removePeerButton.setToolTip(
         self.tr("Remove selected peer"))

      self.editLabelButton = QtWidgets.QPushButton(
         self.tr("Rename"))
      self.editLabelButton.setFixedWidth(peerBtnWidth)
      self.editLabelButton.clicked.connect(
         self.editPeerLabel)
      self.editLabelButton.setEnabled(False)
      self.editLabelButton.setToolTip(
         self.tr("Edit selected peer's label"))

      # Remote IP sub-mode widgets
      self.ipEdit = QtWidgets.QLineEdit('127.0.0.1')
      self.ipEdit.setMinimumWidth(150)
      self.ipEdit.textChanged.connect(
         self.updateCliCommandDisplay)

      self.portEdit = QtWidgets.QLineEdit()
      self.portEdit.setFixedWidth(80)
      self.portEdit.setPlaceholderText(
         str(ARMORYDB_DEFAULT_PORT))
      self.portEdit.textChanged.connect(
         self.updateCliCommandDisplay)

      # Own public key display
      self.ownKeyEdit = QtWidgets.QLineEdit()
      self.ownKeyEdit.setReadOnly(True)
      self.ownKeyEdit.setFont(
         qtdefines.GETFONT('Fixed', 10))
      self.ownKeyEdit.setMinimumWidth(530)
      self.ownKeyEdit.setPlaceholderText(
         self.tr('Load peers DB to view'))

      # CLI command display
      self.cliCommandLabel = QtWidgets.QLabel()
      self.cliCommandLabel.setWordWrap(False)
      self.cliCommandLabel.setStyleSheet(
         f"background-color: "
         f"{htmlColor('SlightBkgdDark')}; "
         f"border: 1px solid {htmlColor('Mid')}; "
         "padding: 8px; font-family: monospace; "
         f"font-size: 10pt; "
         f"color: {htmlColor('Foreground')};")
      self.cliCommandLabel.setSizePolicy(
         QtWidgets.QSizePolicy.Preferred,
         QtWidgets.QSizePolicy.MinimumExpanding)
      self.cliCommandLabel.setText(
         "Settings will appear here")

      # Connect button (only in connect section)
      self.testConnectionButton = QtWidgets.QPushButton(
         self.tr("Connect"))
      self.testConnectionButton.setFixedWidth(140)
      self.testConnectionButton.setToolTip(self.tr(
         "Connect to remote database"))
      self.testConnectionButton.clicked.connect(
         self.onTestConnectionClicked)

   def buildAutoDbSection(self):
      """Build the Automate ArmoryDB section widget."""
      self.autoDbSection = QtWidgets.QWidget()
      sectionLayout = QtWidgets.QVBoxLayout(
         self.autoDbSection)
      sectionLayout.setContentsMargins(20, 4, 0, 8)
      sectionLayout.setSpacing(8)

      # Database directory
      dirFrame = QtWidgets.QGroupBox(
         self.tr('Database Directory'))
      dirFrameLayout = QtWidgets.QVBoxLayout(dirFrame)
      dirFrameLayout.setContentsMargins(12, 12, 12, 12)
      dirFrameLayout.setSpacing(8)
      dirGrid = QtWidgets.QGridLayout()
      dirGrid.setSpacing(8)
      dbDirLabel = QtWidgets.QLabel(self.tr("Location"))
      dirInputLayout, _ = \
         qtdefines.createDirectoryInputLayout(
            self, self.databaseDirEdit,
            self.tr('Select Database Directory'))
      dirGrid.addWidget(dbDirLabel, 0, 0)
      dirGrid.addLayout(dirInputLayout, 0, 1)
      dirGrid.setColumnStretch(1, 1)
      dirFrameLayout.addLayout(dirGrid)
      sectionLayout.addWidget(dirFrame)

      # ArmoryDB configuration
      self.createLocalDatabaseFrame()
      sectionLayout.addWidget(self.localDatabaseFrame)

      # CLI command display
      cliFrame = QtWidgets.QGroupBox(
         self.tr('Generated Command Line'))
      cliLayout = QtWidgets.QVBoxLayout(cliFrame)
      cliLayout.setContentsMargins(12, 12, 12, 12)
      cliLayout.addWidget(self.cliCommandLabel)
      sectionLayout.addWidget(cliFrame)

   def createLocalDatabaseFrame(self):
      """Create the ArmoryDB configuration frame."""
      self.localDatabaseFrame = QtWidgets.QGroupBox(
         self.tr('ArmoryDB Configuration'))
      localDbLayout = QtWidgets.QVBoxLayout(
         self.localDatabaseFrame)
      localDbLayout.setContentsMargins(12, 12, 12, 12)
      localDbLayout.setSpacing(8)

      localDbGrid = QtWidgets.QGridLayout()
      localDbGrid.setSpacing(8)

      dbTypeLabel = QtWidgets.QLabel(
         self.tr("Database Type"))
      ramLabel = QtWidgets.QLabel(
         self.tr("RAM Usage, MB (experimental)"))
      threadLabel = QtWidgets.QLabel(
         self.tr("Thread Count (experimental)"))

      localDbGrid.addWidget(dbTypeLabel, 0, 0)
      localDbGrid.addWidget(self.databaseTypeCombo, 0, 1)
      localDbGrid.addWidget(ramLabel, 1, 0)
      localDbGrid.addWidget(self.ramUsageEdit, 1, 1)
      localDbGrid.addWidget(threadLabel, 2, 0)
      localDbGrid.addWidget(self.threadCountEdit, 2, 1)
      localDbGrid.setColumnStretch(1, 1)

      localDbLayout.addLayout(localDbGrid)

   def buildConnectSection(self):
      """Build the Connect to remote section widget."""
      self.connectSection = QtWidgets.QWidget()
      sectionLayout = QtWidgets.QVBoxLayout(
         self.connectSection)
      sectionLayout.setContentsMargins(20, 4, 0, 8)
      sectionLayout.setSpacing(8)

      # Wrapped in a GroupBox for visual consistency
      connectFrame = QtWidgets.QGroupBox(
         self.tr('Remote Connection'))
      frameLayout = QtWidgets.QVBoxLayout(connectFrame)
      frameLayout.setContentsMargins(12, 12, 12, 12)
      frameLayout.setSpacing(8)

      # Sub-mode combo (IP vs Peer)
      subModeLayout = QtWidgets.QHBoxLayout()
      subModeLayout.setSpacing(8)
      subModeLabel = QtWidgets.QLabel(
         self.tr("Method:"))
      subModeLayout.addWidget(subModeLabel)
      subModeLayout.addWidget(self.remoteSubModeCombo)
      subModeLayout.addStretch()
      frameLayout.addLayout(subModeLayout)

      # Peer sub-frame
      self.peerFrame = QtWidgets.QFrame()
      peerLayout = QtWidgets.QVBoxLayout(self.peerFrame)
      peerLayout.setContentsMargins(0, 0, 0, 0)
      peerLayout.setSpacing(8)

      peerListLabel = QtWidgets.QLabel(
         self.tr("Known Peers:"))
      peerLayout.addWidget(peerListLabel)
      peerLayout.addWidget(self.peerList)

      peerButtonLayout = QtWidgets.QHBoxLayout()
      peerButtonLayout.setSpacing(8)
      peerButtonLayout.addWidget(self.addPeerButton)
      peerButtonLayout.addWidget(self.removePeerButton)
      peerButtonLayout.addWidget(self.editLabelButton)
      peerButtonLayout.addStretch()
      peerLayout.addLayout(peerButtonLayout)

      frameLayout.addWidget(self.peerFrame)

      # IP sub-frame
      self.ipFrame = QtWidgets.QFrame()
      ipLayout = QtWidgets.QVBoxLayout(self.ipFrame)
      ipLayout.setContentsMargins(0, 0, 0, 0)
      ipLayout.setSpacing(8)

      ipGrid = QtWidgets.QGridLayout()
      ipGrid.setSpacing(8)
      ipLabel = QtWidgets.QLabel(
         self.tr("IP Address:"))
      portLabel = QtWidgets.QLabel(self.tr("Port:"))
      ipGrid.addWidget(ipLabel, 0, 0)
      ipGrid.addWidget(self.ipEdit, 0, 1)
      ipGrid.addWidget(portLabel, 0, 2)
      ipGrid.addWidget(self.portEdit, 0, 3)
      ipGrid.setColumnStretch(1, 1)
      ipLayout.addLayout(ipGrid)

      frameLayout.addWidget(self.ipFrame)

      # Own public key (for sharing with peers)
      self.ownKeyFrame = QtWidgets.QFrame()
      ownKeyLayout = QtWidgets.QVBoxLayout(
         self.ownKeyFrame)
      ownKeyLayout.setContentsMargins(0, 12, 0, 0)
      ownKeyLayout.setSpacing(4)
      ownKeyHeader = QtWidgets.QLabel(
         self.tr("Your Public Key:"))
      ownKeyHeader.setToolTip(
         self.tr("Share with peers for mutual auth"))
      ownKeyLayout.addWidget(ownKeyHeader)
      ownKeyLayout.addWidget(self.ownKeyEdit)
      frameLayout.addWidget(self.ownKeyFrame)

      sectionLayout.addWidget(connectFrame)

      # Connect button inside connect section
      btnLayout = QtWidgets.QHBoxLayout()
      btnLayout.setContentsMargins(
         0, 12, 0, qtdefines.UI_BUTTON_SPACING)
      btnLayout.addStretch(1)
      btnLayout.addWidget(self.testConnectionButton)
      btnLayout.addStretch(1)
      sectionLayout.addLayout(btnLayout)

      # Initial: IP visible, peer hidden, ownKey hidden
      self.peerFrame.hide()
      self.ownKeyFrame.hide()

   def getScenario(self):
      """Return the current scenario constant."""
      checkedId = self.modeGroup.checkedId()
      if checkedId == 1:
         subIdx = self.remoteSubModeCombo.currentIndex()
         if subIdx == 1:
            return SCENARIO_REMOTE_PEER
         return SCENARIO_REMOTE_IP
      elif checkedId == 2:
         return SCENARIO_DB_NONE
      return SCENARIO_DB_LOCAL

   def onModeChanged(self, radioId, checked):
      """Show/hide sections based on radio selection."""
      if not checked:
         return
      isAutoDb = radioId == 0
      isConnect = radioId == 1
      self.autoDbSection.setVisible(isAutoDb)
      self.connectSection.setVisible(isConnect)
      self.updateCliCommandDisplay()
      self._updateDefaultCheckbox()
      if isAutoDb:
         self.autoDbSection.adjustSize()

   def onRemoteSubModeChanged(self, index):
      """Toggle IP / Peer sub-frames in connect section."""
      isPeer = index == 1
      self.ipFrame.setVisible(not isPeer)
      self.peerFrame.setVisible(isPeer)
      self.ownKeyFrame.setVisible(isPeer)

      self.updateCliCommandDisplay()
      self._updateDefaultCheckbox()

   def _updateDefaultCheckbox(self):
      """Disable checkbox when current scenario matches saved default."""
      isDefault = \
         self.getScenario() == self._savedDefaultScenario
      self.setDefaultCheckbox.setEnabled(not isDefault)
      self.setDefaultCheckbox.setChecked(isDefault)

   def updateCliCommandDisplay(self):
      """Update the CLI command display.

      Informational only. Actual connection is via bridge.
      """
      scenario = self.getScenario()
      if scenario == SCENARIO_DB_LOCAL:
         dbType = self.databaseTypeCombo.currentText()
         ramUsage = self.ramUsageEdit.text() or '50'
         threadCount = \
            self.threadCountEdit.text() or '4'
         dbDir = self.databaseDirEdit.text() \
            or ARMORY_DB_DIR
         dataDir = ARMORY_HOME_DIR \
            or '/path/to/armory'
         if BTC_HOME_DIR:
            satoshiDir = os.path.join(
               BTC_HOME_DIR, 'blocks')
         else:
            satoshiDir = '/path/to/bitcoin/blocks'
         dbTypeArg = 'DB_SUPER' \
            if dbType == 'Supernode' else 'DB_BARE'
         cmdParts = [
            '# Local ArmoryDB (bridge: automateDb)',
            'ArmoryDB', '--ephemeral',
            f'--db-type={dbTypeArg}',
            f'--ram-usage={ramUsage}',
            f'--thread-count={threadCount}',
            f'--datadir="{dataDir}"',
            f'--dbdir="{dbDir}"',
            f'--satoshi-datadir="{satoshiDir}"'
         ]
      elif scenario == SCENARIO_REMOTE_PEER:
         selected = self.peerList.currentItem()
         peer = selected.data(QtCore.Qt.UserRole) \
            if selected else None
         peerDisp = selected.text() if selected \
            else 'none'
         authMode = peer.authModeStr() \
            if peer else '?'
         cmdParts = [
            '# Remote via peer (connectToPeer)',
            f'Peer: {peerDisp}',
            f'Auth: {authMode}'
         ]
      elif scenario == SCENARIO_REMOTE_IP:
         ipAddr = self.ipEdit.text().strip() \
            or 'not set'
         portText = self.portEdit.text().strip() \
            or str(ARMORYDB_DEFAULT_PORT)
         cmdParts = [
            '# Remote via IP (connectToIp)',
            f'IP: {ipAddr}',
            f'Port: {portText}',
            'Auth: 1-way only'
         ]
      else:
         cmdParts = [
            '# Offline mode',
            'No database connection']
      commandLine = '\n  '.join(cmdParts)
      self.cliCommandLabel.setText(
         f"Settings:\n  {commandLine}")

   def onTestConnectionClicked(self):
      """Emit testConnectionRequested for parent to handle."""
      LOGINFO("Connect requested from DatabaseTab")
      self.testConnectionRequested.emit()

   def refreshPeerList(self):
      """Refresh peer list widget from bridge."""
      self.peerList.clear()
      self.ownKey = ''

      if not self.peersDbLoaded:
         self._showPeerListHint(
            self.tr("Peers database not loaded"))
         return

      peerCount = 0
      bridgePeers = TheBridge.dbSetup.listPeers()
      for bp in bridgePeers:
         peer = PeerData.fromBridgePeer(bp)
         if peer.isOwn():
            self.ownKey = peer.key
            self.ownKeyEdit.setText(peer.key)
            continue

         peerCount += 1
         names = ' '.join(peer.names)
         label = peer.label if peer.label \
            and peer.label != 'N/A' else ''
         authTag = peer.authModeStr()
         keyPrefix = peer.key[:8]
         if label:
            display = ' '.join([
               f"{keyPrefix}\u2026",
               f"{label} \u2014 {names}",
               f"[{authTag}]"])
         else:
            display = ' '.join([
               f"{keyPrefix}\u2026",
               names, f"[{authTag}]"])
         item = QtWidgets.QListWidgetItem(display)
         item.setData(QtCore.Qt.UserRole, peer)
         item.setToolTip(peer.key)
         self.peerList.addItem(item)

      if peerCount == 0:
         self._showPeerListHint(
            self.tr(
               "No peers yet, use Add Peer "
               "to add one"))
         return

      savedKey = TheSettings.getSettingOrSetDefault(
         'RemotePeerKey', '')
      if savedKey:
         for i in range(self.peerList.count()):
            item = self.peerList.item(i)
            peer = item.data(QtCore.Qt.UserRole)
            if peer and peer.key == savedKey:
               self.peerList.setCurrentItem(item)
               break

   def _showPeerListHint(self, text):
      """Show placeholder-style hint in the empty peer list."""
      hint = QtWidgets.QListWidgetItem(text)
      hint.setFlags(QtCore.Qt.NoItemFlags)
      hint.setForeground(
         self.peerList.palette().color(
            QtGui.QPalette.ColorGroup.Disabled,
            QtGui.QPalette.ColorRole.Text))
      font = hint.font()
      font.setItalic(True)
      hint.setFont(font)
      self.peerList.addItem(hint)

   def _showDefaultHint(self, dbScenario):
      """Place the 'default' hint next to the saved radio."""
      rowMap = {
         SCENARIO_DB_LOCAL: self.autoDbRow,
         SCENARIO_REMOTE_IP: self.connectRow,
         SCENARIO_REMOTE_PEER: self.connectRow,
         SCENARIO_DB_NONE: self.offlineRow,
      }
      targetRow = rowMap.get(
         dbScenario, self.autoDbRow)
      targetRow.insertWidget(1, self.defaultHintLabel)
      self.defaultHintLabel.show()

   def addPeerFromTab(self):
      """Add a peer directly from the database tab."""
      if not self.peersDbLoaded:
         QtWidgets.QMessageBox.warning(
            self,
            self.tr('Peers DB Required'),
            self.tr('Load the peers database first '
               'to add peers.'))
         return
      dialog = AddPeerDialog(self)
      if dialog.exec_() == QtWidgets.QDialog.Accepted:
         peerKey = dialog.peerKey
         address = dialog.address
         label = dialog.peerLabel
         result = TheBridge.dbSetup.addPeer(
            peerKey, [address], label)
         if result.success:
            LOGINFO(f"Added peer: {address}")
            self.refreshPeerList()
         else:
            QtWidgets.QMessageBox.warning(
               self, self.tr('Add Peer Failed'),
               self.tr('Failed to add peer: {}').format(
                  result.error))

   def removePeerFromTab(self):
      """Remove the selected peer from the peers database."""
      selected = self.peerList.currentItem()
      if not selected:
         return
      peer = selected.data(QtCore.Qt.UserRole)
      if not peer:
         return

      reply = QtWidgets.QMessageBox.question(
         self,
         self.tr('Remove Peer'),
         self.tr('Remove peer {}?').format(
            selected.text()),
         QtWidgets.QMessageBox.Yes
            | QtWidgets.QMessageBox.No,
         QtWidgets.QMessageBox.No)
      if reply != QtWidgets.QMessageBox.Yes:
         return

      result = TheBridge.dbSetup.removePeer(peer.key)
      if result.success:
         LOGINFO(f"Removed peer: {selected.text()}")
         self.refreshPeerList()
      else:
         QtWidgets.QMessageBox.warning(
            self, self.tr('Remove Peer Failed'),
            self.tr('Failed to remove peer: {}').format(
               result.error))

   def editPeerLabel(self):
      """Edit the label of the selected peer."""
      selected = self.peerList.currentItem()
      if not selected:
         return
      peer = selected.data(QtCore.Qt.UserRole)
      if not peer:
         return

      newLabel, ok = QtWidgets.QInputDialog.getText(
         self,
         self.tr('Edit Peer Label'),
         self.tr('New label for peer:'),
         QtWidgets.QLineEdit.Normal,
         peer.label if peer.label != 'N/A' else '')
      if not ok:
         return

      result = TheBridge.dbSetup.setPeerLabel(
         peer.key, newLabel.strip())
      if result.success:
         self.refreshPeerList()
      else:
         QtWidgets.QMessageBox.warning(
            self,
            self.tr('Edit Label Failed'),
            self.tr(
               'Failed to update label: {}'
            ).format(result.error))

   def setPeersDbLoaded(self, loaded):
      """Update peers database loaded state."""
      self.peersDbLoaded = loaded
      self.refreshPeerList()
      self.addPeerButton.setEnabled(loaded)
      self.removePeerButton.setEnabled(loaded)
      self.editLabelButton.setEnabled(loaded)
      if not loaded:
         self.ownKeyEdit.clear()
         self.ownKeyEdit.setPlaceholderText(
            self.tr('Peers DB not loaded'))

   def setDbSettingsLocked(self, locked):
      """Lock or unlock DB settings after connection.

      C++ bridge bug: bdvPtr_ is not reset after cleanupDb,
      so reconnection is not possible on the same session.
      Lock settings to prevent changes after success.
      """
      self.autoDbRadio.setEnabled(not locked)
      self.connectRadio.setEnabled(not locked)
      self.offlineRadio.setEnabled(not locked)
      self.databaseDirEdit.setEnabled(not locked)
      self.databaseTypeCombo.setEnabled(not locked)
      self.ramUsageEdit.setEnabled(not locked)
      self.threadCountEdit.setEnabled(not locked)
      self.remoteSubModeCombo.setEnabled(not locked)
      self.ipEdit.setEnabled(not locked)
      self.portEdit.setEnabled(not locked)
      self.testConnectionButton.setEnabled(not locked)
      if locked:
         self.testConnectionButton.setText(
            self.tr("Connected"))

   def loadSettings(self):
      """Load database tab settings from configuration."""
      self.databaseDirEdit.setText(
         os.path.normpath(ARMORY_DB_DIR))

      dbScenario = TheSettings.getSettingOrSetDefault(
         'DBScenarioDefault', SCENARIO_DB_LOCAL)
      # Migrate old scenario names
      if dbScenario == "Remote Database":
         oldMode = TheSettings.getSettingOrSetDefault(
            'RemoteMode', '')
         if 'Peer' in oldMode:
            dbScenario = SCENARIO_REMOTE_PEER
         else:
            dbScenario = SCENARIO_REMOTE_IP
      elif dbScenario in (
            "Remote (by Peer)", "Remote, by Peer"):
         dbScenario = SCENARIO_REMOTE_PEER
      elif dbScenario in (
            "Remote (by IP)", "Remote, by IP"):
         dbScenario = SCENARIO_REMOTE_IP

      # Select correct radio and sub-mode
      if isRemoteScenario(dbScenario):
         self.connectRadio.setChecked(True)
         if dbScenario == SCENARIO_REMOTE_PEER:
            self.remoteSubModeCombo.setCurrentIndex(1)
         else:
            self.remoteSubModeCombo.setCurrentIndex(0)
      elif dbScenario == SCENARIO_DB_NONE:
         self.offlineRadio.setChecked(True)
      else:
         self.autoDbRadio.setChecked(True)

      self._savedDefaultScenario = dbScenario
      self.setDefaultCheckbox.setChecked(True)
      self.setDefaultCheckbox.setEnabled(False)
      self._showDefaultHint(dbScenario)

      # Warn if an existing database is detected
      dbIsBootstrapped = False
      if os.path.exists(ARMORY_DB_DIR):
         dbFiles = os.listdir(ARMORY_DB_DIR)
         dbIsBootstrapped = any(
            f.endswith('.db') or f.endswith('.ldb')
            for f in dbFiles)

      if dbIsBootstrapped and self.dbBootstrapLabel is None:
         self.dbBootstrapLabel = QtWidgets.QLabel(
            self.tr(
               "Existing database detected in "
               "data directory"))
         self.dbBootstrapLabel.setStyleSheet(
            f"color: {htmlColor('TextWarn')}; "
            "font-style: italic;")
         layout = self.layout()
         if layout:
            layout.addWidget(self.dbBootstrapLabel)

      # Fire initial visibility
      self.onModeChanged(
         self.modeGroup.checkedId(), True)

      # Scenario-specific settings
      dbScenario = self.getScenario()
      if dbScenario == SCENARIO_DB_LOCAL:
         dbTypeSetting = \
            TheSettings.getSettingOrSetDefault(
               'DBType', 'DB_BARE')
         if dbTypeSetting == 'DB_SUPER':
            self.databaseTypeCombo.setCurrentText(
               'Supernode')
         else:
            self.databaseTypeCombo.setCurrentText(
               'Full Database')
         self.ramUsageEdit.setText(str(
            TheSettings.getSettingOrSetDefault(
               'RAMUsage', 50)))
         self.threadCountEdit.setText(str(
            TheSettings.getSettingOrSetDefault(
               'ThreadCount', 4)))
      elif dbScenario == SCENARIO_REMOTE_IP:
         savedIp = TheSettings.getSettingOrSetDefault(
            'RemoteIpAddr', '')
         savedPort = TheSettings.getSettingOrSetDefault(
            'RemoteIpPort', '')
         if savedIp:
            self.ipEdit.setText(savedIp)
         if savedPort:
            self.portEdit.setText(str(savedPort))

   def collectSettings(self):
      """Return current database config from UI."""
      scenario = self.getScenario()
      isLocal = scenario == SCENARIO_DB_LOCAL
      isPeer = scenario == SCENARIO_REMOTE_PEER
      isIp = scenario == SCENARIO_REMOTE_IP

      peerKey = ''
      ipAddr = ''
      ipPort = ''

      if isPeer:
         selected = self.peerList.currentItem()
         if selected \
               and selected.data(QtCore.Qt.UserRole):
            peer = selected.data(QtCore.Qt.UserRole)
            peerKey = peer.key
      elif isIp:
         ipAddr = self.ipEdit.text().strip()
         portText = self.portEdit.text().strip()
         ipPort = portText if portText \
            else str(ARMORYDB_DEFAULT_PORT)

      return {
         'dbPath': self.databaseDirEdit.text(),
         'scenario': scenario,
         'setAsDefault':
            self.setDefaultCheckbox.isChecked(),
         'typeDisp':
            self.databaseTypeCombo.currentText()
            if isLocal else '',
         'ram': self.ramUsageEdit.text()
            if isLocal else '',
         'threads': self.threadCountEdit.text()
            if isLocal else '',
         'peerKey': peerKey,
         'ipAddr': ipAddr,
         'ipPort': ipPort,
      }

   def validate(self):
      """Validate database tab settings."""
      scenario = self.getScenario()
      if scenario == SCENARIO_DB_LOCAL:
         ramText = self.ramUsageEdit.text().strip()
         threadText = self.threadCountEdit.text().strip()
         if ramText:
            try:
               ram = int(ramText)
               if ram < 1 or ram > MAX_RAM_USAGE:
                  raise ValueError()
            except ValueError:
               msg = self.tr('RAM usage must be an integer between 1 and {}.')
               QtWidgets.QMessageBox.warning(self, self.tr('Invalid RAM Usage'),
                  msg.format(MAX_RAM_USAGE))
               return False
         if threadText:
            try:
               threads = int(threadText)
               if threads < 1 or threads > MAX_THREAD_COUNT:
                  raise ValueError()
            except ValueError:
               msg = self.tr('Thread count must be between '
                  '1 and {}.')
               QtWidgets.QMessageBox.warning(
                  self, self.tr('Invalid Thread Count'),
                  msg.format(MAX_THREAD_COUNT))
               return False
      elif scenario == SCENARIO_REMOTE_PEER:
         selected = self.peerList.currentItem()
         if not selected \
               or not selected.data(QtCore.Qt.UserRole):
            QtWidgets.QMessageBox.warning(
               self,
               self.tr('No Peer Selected'),
               self.tr('Select a known peer or use '
                  'Connect to IP mode.'))
            return False
      elif scenario == SCENARIO_REMOTE_IP:
         ipAddr = self.ipEdit.text().strip()
         if not ipAddr:
            QtWidgets.QMessageBox.warning(
               self,
               self.tr('Missing IP Address'),
               self.tr('Enter the remote database IP.'))
            return False
         portText = self.portEdit.text().strip()
         if portText:
            try:
               port = int(portText)
               if port < 1 or port > 65535:
                  raise ValueError()
            except ValueError:
               QtWidgets.QMessageBox.warning(
                  self,
                  self.tr('Invalid Port'),
                  self.tr('Port must be 1-65535.'))
               return False
      return True

   def validateDbPath(self):
      """
      Validate database directory exists, create if needed.
      Returns True if valid/created, False on error.
      """
      dbPath = self.databaseDirEdit.text()
      if not os.path.exists(dbPath):
         try:
            os.makedirs(dbPath)
         except (OSError, PermissionError) as e:
            QtWidgets.QMessageBox.critical(
               self,
               self.tr('Error'),
               self.tr('Cannot create database directory: {}').format(str(e))
            )
            return False
      return True
