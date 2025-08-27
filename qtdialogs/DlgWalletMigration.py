##############################################################################
#                                                                            #
#  Copyright (C) 2025, goatpig                                               #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################

from armoryengine.CppBridge import ServerPush, TheBridge
from armoryengine.ArmoryUtils import unixTimeToFormatStr
from ui.QtExecuteSignal import TheSignalExecution
from ui.Wizards import SetPassphrasePage, VerifyPassphrasePage, \
   WalletProgressPage

from qtpy import QtCore, QtWidgets
from qtdialogs.qtdefines import QRichLabel, HLINE, \
   AdvancedOptionsFrame
from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.DlgUnlockWallet import DlgUnlockWallet
from armorycolors import htmlColor

# --- Constants for UI layout ---
gridHSpacing = 10
gridVSpacing = 6

################################################################################
def styledLabel(text):
   """Creates a styled label with consistent formatting for field labels."""
   lbl = QtWidgets.QLabel(text)
   lbl.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
   lbl.setStyleSheet('font-size: 9pt; font-weight: 500;')
   lbl.setWordWrap(False)
   lbl.setSizePolicy(QtWidgets.QSizePolicy.Minimum,
      QtWidgets.QSizePolicy.Minimum)
   return lbl

####
def styledValue(text):
   """Creates a styled label with consistent formatting for field values."""
   val = QtWidgets.QLabel(str(text))
   val.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
   val.setStyleSheet('font-size: 9pt;')
   val.setWordWrap(True)
   val.setSizePolicy(QtWidgets.QSizePolicy.Preferred,
      QtWidgets.QSizePolicy.Minimum)
   return val

####
def createHeadingLabel(text):
   """Creates a styled heading label for dialog sections."""
   lbl = QRichLabel(text)
   lbl.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)
   lbl.setContentsMargins(0, 0, 0, 0)
   lbl.setWordWrap(False)
   return lbl

####
def createSubtextLabel(text):
   """Creates a styled subtext label for additional information."""
   lbl = QRichLabel(text)
   lbl.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)
   lbl.setContentsMargins(0, 0, 0, 0)
   lbl.setWordWrap(False)
   return lbl

################################################################################
class DlgUnlockMigratingWallet(DlgUnlockWallet):
   """Unlock dialog specifically for wallet migration."""
   def __init__(self, parent, main):
      super().__init__(
         wltID=parent.walletData.walletId,
         parent=parent, main=main,
         unlockMsg="Unlock Wallet To Migrate")
      self.passphrase = None

   def reply(self, passphrase):
      """Handle the reply with the entered passphrase."""
      self.passphrase = passphrase
      if passphrase:
         self.accept()
      else:
         self.reject()

   def rejectPassphrase(self):
      """Handle cancellation of passphrase entry."""
      if self.parent.handleUnlockCancelForWatchOnly():
         # The parent dialog handled the logic for Yes/No. This dialog's
         # only remaining job is to close itself.
         self.reject()

################################################################################
class DlgWalletMigration(ArmoryDialog, ServerPush):
   """Dialog for migrating a legacy wallet to the new format."""
   def __init__(self, parent, main, wltPath, walletData):
      ServerPush.__init__(self)
      ArmoryDialog.__init__(self, parent, main)

      self.initMemberVariables(wltPath, walletData)
      self.setupCancelButton()
      self.initWizardPages()
      self.setupPageLayouts()
      self.setupMainLayout()
      self.connectSignals()

   def initMemberVariables(self, wltPath, walletData):
      """Initialize all member variables with default values."""
      self.walletPath = wltPath
      self.walletData = walletData
      self.storedPassphrase = None
      self.doneBtn = None
      self.resultLabel = None
      self.errorLabel = None
      self.migrationComplete = False
      self.migrationFailed = False
      self.migrationStarted = False
      self.wired = False
      self.newPassphrase = None
      self.cancellationSent = False

   def setupCancelButton(self):
      """Create and configure the cancel button."""
      self.btnCancel = QtWidgets.QPushButton(self.tr('Cancel'))
      self.btnCancel.setSizePolicy(QtWidgets.QSizePolicy.Minimum,
         QtWidgets.QSizePolicy.Minimum)

   def initWizardPages(self):
      """Initialize all wizard pages for the migration process."""
      self.page1 = self.setupPage1()
      self.setPassphrasePage = SetPassphrasePage(self)
      self.verifyPassphrasePage = VerifyPassphrasePage(self)
      self.walletProgressPage = WalletProgressPage(self)

   def setupPageLayouts(self):
      """Configure layouts for all wizard pages."""
      self.setupProgressPage()
      self.setupPassphrasePage()
      self.setupVerifyPage()

   def setupProgressPage(self):
      """Set up the progress page"""
      self.doneBtn = QtWidgets.QPushButton(self.tr('Done'))
      self.doneBtn.setEnabled(False)
      self.doneBtn.clicked.connect(self.close)
      btnLayout = QtWidgets.QHBoxLayout()
      btnLayout.addStretch(1)
      btnLayout.addWidget(self.doneBtn)
      self.walletProgressPage.pageFrame.layout().addLayout(btnLayout)

   def setupPassphrasePage(self):
      """Set up the passphrase entry page"""
      nextBtn = QtWidgets.QPushButton(self.tr('Next'))
      nextBtn.setEnabled(False)
      nextBtn.clicked.connect(self.nextFromSetPassphrase)

      # Add cancel button for passphrase page
      cancelBtn = QtWidgets.QPushButton(self.tr('Cancel'))
      cancelBtn.clicked.connect(self.cancelPassphraseSetup)

      # Create tab widget
      tabWidget = QtWidgets.QTabWidget()

      # Add the original passphrase frame as the first tab
      tabWidget.addTab(self.setPassphrasePage.pageFrame, self.tr("Set Passphrase"))

      # Add the KDF options as the second tab
      self.advancedOptionsTab = AdvancedOptionsFrame(self, self.main)
      tabWidget.addTab(self.advancedOptionsTab, self.tr("Advanced Options"))

      # Create button layout
      btnLayout = self.createButtonLayout(cancelBtn, nextBtn)

      # Add both tab widget and buttons directly to the page's layout
      # WITHOUT calling setLayout (which would destroy existing widgets)
      pageLayout = self.setPassphrasePage.layout()
      pageLayout.addWidget(tabWidget)
      pageLayout.addLayout(btnLayout)

      self.setupPassphraseValidation(nextBtn)

   def setupPassphraseValidation(self, nextBtn):
      """Set up validation for the passphrase entry fields."""
      def enableNextBtn():
         pw1 = self.setPassphrasePage.pageFrame.editPasswd1.text()
         pw2 = self.setPassphrasePage.pageFrame.editPasswd2.text()
         def isASCII(s):
            try:
               s.encode('ascii')
               return True
            except Exception:
               return False
         nextBtn.setEnabled(bool(pw1) and pw1 == pw2 and len(pw1) >= 5 \
            and isASCII(pw1) and isASCII(pw2))
      self.setPassphrasePage.pageFrame.passphraseCallback = enableNextBtn
      enableNextBtn()

   def setupVerifyPage(self):
      """Set up the passphrase verification page"""
      doneBtn = QtWidgets.QPushButton(self.tr('Done'))
      doneBtn.clicked.connect(self.nextFromVerifyPassphrase)

      # Add cancel button for verify page
      cancelBtn = QtWidgets.QPushButton(self.tr('Cancel'))
      cancelBtn.clicked.connect(self.cancelPassphraseSetup)

      # Create button layout
      btnLayout = self.createButtonLayout(cancelBtn, doneBtn)

      verifyLayout = self.verifyPassphrasePage.pageFrame.layout()
      row = verifyLayout.rowCount()
      # The original button was in column 1. Add this layout there.
      verifyLayout.addLayout(btnLayout, row, 1)

   def setupMainLayout(self):
      """Set up the main layout with all pages in a stack."""
      self.stack = QtWidgets.QStackedLayout()
      self.stack.addWidget(self.page1)
      self.stack.addWidget(self.setPassphrasePage)
      self.stack.addWidget(self.verifyPassphrasePage)
      self.stack.addWidget(self.walletProgressPage)
      self.setLayout(self.stack)
      self.setWindowTitle(self.tr('Wallet Migration'))
      self.setSizePolicy(QtWidgets.QSizePolicy.Preferred,
         QtWidgets.QSizePolicy.Preferred)
      self.layout().setSizeConstraint(QtWidgets.QLayout.SetFixedSize)
      self.stack.setCurrentIndex(0)
      self.setFixedSize(self.sizeHint())

   def connectSignals(self):
      """Connect all signals to their handlers."""
      self.btnNext.clicked.connect(self.proceedWithMigration)
      self.btnCancel.clicked.connect(self.reject)

   ####
   def createButtonLayout(self, *buttons):
      """Create a horizontal layout with buttons aligned to the right."""
      btnLayout = QtWidgets.QHBoxLayout()
      btnLayout.setSpacing(6)  # Reduce spacing between buttons (default is usually 12-15)
      btnLayout.addStretch(1)  # Pushes buttons to the right
      for btn in buttons:
         btnLayout.addWidget(btn)
      return btnLayout

   def createFailureLabel(self, message):
      """Create and display a failure label with consistent styling."""
      self.resultLabel = QtWidgets.QLabel(message)
      self.resultLabel.setAlignment(QtCore.Qt.AlignHCenter)
      self.resultLabel.setStyleSheet(
         f"font-size: 11pt; font-weight: bold; color: {htmlColor('TextRed')};"
         f"background: transparent; border: none;")
      layout = self.walletProgressPage.pageFrame.layout()
      layout.insertWidget(
         layout.count() - 1, self.resultLabel, alignment=QtCore.Qt.AlignHCenter)

   def navigateToProgressPage(self):
      """Navigate to the progress page and resize the dialog."""
      self.stack.setCurrentWidget(self.walletProgressPage)
      self.resize(self.stack.currentWidget().sizeHint())

   def setupPage1(self):
      """Sets up the first page (wallet details) of the migration dialog"""
      # Create main container widget
      page1 = QtWidgets.QWidget()
      layout = QtWidgets.QVBoxLayout()
      layout.setContentsMargins(14, 6, 14, 8)
      layout.setSpacing(8)

      # Header
      title = createHeadingLabel(self.tr
         ('<span style="font-size:16pt;"><b>Wallet Details</b></span>'))
      layout.addWidget(title, alignment=QtCore.Qt.AlignHCenter)
      subtitle = createSubtextLabel(self.tr
         ('<span style="font-size:10pt;">Review the details of the wallet you selected</span>'))
      layout.addWidget(subtitle, alignment=QtCore.Qt.AlignHCenter)
      layout.addSpacing(2)

      # Top horizontal line
      hline1 = QtWidgets.QHBoxLayout()
      hline1.setContentsMargins(14, 6, 14, 0)
      hline1.addWidget(HLINE())
      layout.addLayout(hline1)
      layout.addSpacing(6)

      # Wallet details group box
      groupBox = QtWidgets.QGroupBox(self.tr("Wallet Information"))
      vbox = QtWidgets.QVBoxLayout()
      vbox.setContentsMargins(12, 12, 12, 12)
      vbox.setSpacing(8)
      grid = QtWidgets.QGridLayout()
      grid.setContentsMargins(0, 0, 0, 0)
      grid.setHorizontalSpacing(gridHSpacing)
      grid.setVerticalSpacing(gridVSpacing)
      grid.setColumnStretch(0, 0)
      grid.setColumnStretch(1, 0)
      row = 0
      grid.addWidget(styledLabel(self.tr('Wallet ID:')), row, 0, QtCore.Qt.AlignLeft)
      grid.addWidget(styledValue(self.walletData.walletId), row, 1, QtCore.Qt.AlignLeft)
      row += 1
      grid.addWidget(styledLabel(self.tr('Wallet Type:')), row, 0, QtCore.Qt.AlignLeft)
      grid.addWidget(styledValue(self.walletData.which()), row, 1, QtCore.Qt.AlignLeft)
      row += 1
      grid.addWidget(styledLabel(self.tr('Version:')), row, 0, QtCore.Qt.AlignLeft)
      grid.addWidget(styledValue(self.walletData.seedVersion), row, 1, QtCore.Qt.AlignLeft)
      row += 1

      # Determine security status
      if self.walletData.watchingOnly:
         securityStatus = self.tr('Watching-only')
      elif self.walletData.encrypted:
         securityStatus = self.tr('Encrypted')
      else:
         securityStatus = self.tr('No Encryption')

      grid.addWidget(styledLabel(self.tr('Security:')), row, 0, QtCore.Qt.AlignLeft)
      grid.addWidget(styledValue(securityStatus), row, 1, QtCore.Qt.AlignLeft)
      row += 1
      grid.addWidget(styledLabel(self.tr('Label:')), row, 0, QtCore.Qt.AlignLeft)
      grid.addWidget(styledValue(self.walletData.label), row, 1, QtCore.Qt.AlignLeft)
      row += 1
      grid.addWidget(styledLabel(self.tr('Description:')), row, 0, QtCore.Qt.AlignLeft)
      grid.addWidget(styledValue(self.walletData.description), row, 1, QtCore.Qt.AlignLeft)
      row += 1

      # Address count with N/A handling for invalid values
      addressCount = self.walletData.addressCount
      if addressCount is None or addressCount < 0:
         addressCountDisplay = self.tr('N/A')
      else:
         addressCountDisplay = str(addressCount)

      grid.addWidget(styledLabel(self.tr('Address count:')), row, 0, QtCore.Qt.AlignLeft)
      grid.addWidget(styledValue(addressCountDisplay), row, 1, QtCore.Qt.AlignLeft)
      row += 1

      # Top used address with N/A handling for invalid values
      highestUsedIndex = self.walletData.highestUsedIndex
      if highestUsedIndex is None or highestUsedIndex < 0:
         highestUsedIndexDisplay = self.tr('N/A')
      else:
         highestUsedIndexDisplay = str(highestUsedIndex)

      grid.addWidget(styledLabel(self.tr('Top used address:')), row, 0, QtCore.Qt.AlignLeft)
      grid.addWidget(styledValue(highestUsedIndexDisplay), row, 1, QtCore.Qt.AlignLeft)
      row += 1

      # Last used timestamp with N/A handling for invalid values
      try:
         lastUsedDisplay = unixTimeToFormatStr(self.walletData.timestamp, '%Y/%m/%d %H:%M')
      except:
         lastUsedDisplay = self.tr('N/A')

      grid.addWidget(styledLabel(self.tr('Last Modified:')), row, 0, QtCore.Qt.AlignLeft)
      grid.addWidget(styledValue(lastUsedDisplay), row, 1, QtCore.Qt.AlignLeft)
      row += 1
      vbox.addLayout(grid)
      groupBox.setLayout(vbox)
      layout.addWidget(groupBox, alignment=QtCore.Qt.AlignHCenter)

      # Bottom horizontal line
      hline2 = QtWidgets.QHBoxLayout()
      hline2.setContentsMargins(14, 6, 14, 0)
      hline2.addWidget(HLINE())
      layout.addLayout(hline2)
      layout.addSpacing(6)

      # Button row
      btnFrame1 = self.createPage1Buttons()
      btnFrame1.addSpacing(12)
      layout.addLayout(btnFrame1)

      page1.setLayout(layout)
      return page1

   def createPage1Buttons(self):
      """Creates the button row for page 1 based on wallet type and encryption."""
      # Create all buttons here to ensure they exist before being added

      # Set button text based on wallet type
      if self.walletData.which() == 'legacy':
         nextButtonText = self.tr('Migrate')
      else:
         nextButtonText = self.tr('Import')

      self.btnNext = QtWidgets.QPushButton(nextButtonText)
      self.btnCancel = QtWidgets.QPushButton(self.tr('Cancel'))

      btnFrame1 = QtWidgets.QHBoxLayout()
      btnFrame1.addStretch(1)
      btnFrame1.addWidget(self.btnCancel)
      btnFrame1.addWidget(self.btnNext)
      return btnFrame1

   def processUnlockRequest(self, ids):
      """Handle unlock requests from the backend."""
      # Ignore unlock requests if migration is already complete
      if self.migrationComplete:
         return

      dlgUnlock = DlgUnlockMigratingWallet(parent=self, main=self.main)
      dlgUnlock.setIds(ids)
      
      if dlgUnlock.passphrase:
         self.storedPassphrase = dlgUnlock.passphrase
         packet = self.getNewPacket()
         packet.unlockRequest = dlgUnlock.passphrase
         packet.success = True
         self.reply()

   ####
   def processSetNewPassphrase(self, isPriv, reusePassphrase=False):
      """Process the request to set a new passphrase."""
      packet = self.getNewPacket()
      passPacket = packet.init("setPassphrase")

      if isPriv:
         # private keys passphrase, do we reuse old one or get a fresh one?
         if reusePassphrase == False:
            # User went through wizard to create new passphrase
            packet.success = True
            passPacket.passphrase = self.newPassphrase

            # Get KDF parameters from the advanced options tab
            kdfSec = self.advancedOptionsTab.getKdfSec()
            if kdfSec <= 0:
               kdfSec = 2.0  # Default fallback
            passPacket.kdfTargetMs = int(kdfSec * 1000)

            kdfBytes = self.advancedOptionsTab.getKdfBytes()
            if kdfBytes <= 0:
               kdfBytes = 128 * 1024 * 1024  # Default fallback (128 MB)
            passPacket.kdfTargetMB = int(kdfBytes / (1024**2))

            # Clean up wizard state
            self.newPassphrase = None
         else:
            # user wants to reuse old passphrase, feed that instead
            if not self.storedPassphrase:
               raise Exception("Reuse passphrase requested but no passphrase stored")

            packet.success = True
            passPacket.passphrase = self.storedPassphrase
            passPacket.kdfTargetMs = 2000
            passPacket.kdfTargetMB = 128

            # Clean up stored passphrase
            self.storedPassphrase = None
      else:
         # control passphrase, return empty for now
         packet.success = True
         passPacket.passphrase = ""
         passPacket.kdfTargetMs = 250
         passPacket.kdfTargetMB = 0

      self.reply()

   ####
   def processCallback(self, payload):
      """Process callbacks from the backend during wallet migration."""
      if payload.which() == 'cleanup':
         self.migrationComplete = True
         self.stack.setCurrentWidget(self.walletProgressPage)
         self.walletProgressPage.pageFrame.progressBar.hide()

         # Create and show result label
         if self.migrationFailed:
            self.createFailureLabel(
               f"Migration of Wallet {self.walletData.walletId} failed!")

            # Show backend error message below the FAILURE label
            errorMsg = getattr(payload, 'errorMessage', None)
            if errorMsg:
               self.errorLabel = QtWidgets.QLabel(errorMsg)
               self.errorLabel.setAlignment(QtCore.Qt.AlignHCenter)
               self.errorLabel.setStyleSheet(
                  f"font-size: 8pt; color: {htmlColor('TextRed')};"
                  "background: transparent;"
                  "border: none;")
               layout = self.walletProgressPage.pageFrame.layout()
               layout.insertWidget(
                  layout.count() - 1, self.errorLabel,
                  alignment=QtCore.Qt.AlignHCenter)
         else:
            self.resultLabel = QtWidgets.QLabel(
               f"Migration of Wallet {self.walletData.walletId} was successful!")
            self.resultLabel.setAlignment(QtCore.Qt.AlignHCenter)
            self.resultLabel.setStyleSheet(
               "font-size: 11pt; font-weight: bold; color: '#00FF00';"
               "background: transparent; border: none;")
            layout = self.walletProgressPage.pageFrame.layout()
            layout.insertWidget(
               layout.count() - 1, self.resultLabel,
               alignment=QtCore.Qt.AlignHCenter)

         # Enable the Done button when process completes
         self.doneBtn.setEnabled(True)
         return

      elif payload.which() == 'unlockRequest':
         self.processUnlockRequest(payload.unlockRequest)
         return

      elif payload.which() == 'setPassphrase':
         # Passphrase already stored by unlock dialog in self.storedPassphrase
         notif = payload.setPassphrase
         if notif.which() == 'controlPass':
            self.processSetNewPassphrase(False)
         elif notif.which() == 'privatePass':
            choice = self.promptPassphraseReuseChoice()
            if choice == 'reuse':
               # User chose to reuse old passphrase
               self.processSetNewPassphrase(True, reusePassphrase=True)
               self.navigateToProgressPage()
            elif choice == 'new':
               # User chose to create a new passphrase via wizard
               self.setPassphrasePage.pageFrame.editPasswd1.clear()
               self.setPassphrasePage.pageFrame.editPasswd2.clear()
               self.stack.setCurrentWidget(self.setPassphrasePage)
               self.resize(self.stack.currentWidget().sizeHint())
            else:
               QtWidgets.QMessageBox.critical(
                  self,
                  self.tr('Migration Error'),
                  self.tr('Migration was cancelled or failed!')
               )
               self.reject()
         return

      elif payload.which() == 'walletProgress':
         # Show progress page and update as events arrive
         self.stack.setCurrentWidget(self.walletProgressPage)
         notif = payload.walletProgress
         if notif.which() == 'createFile':
            self.walletProgressPage.pageFrame.updateProgress(
               f"creating file: {notif.createFile}")
         elif notif.which() == 'initFile':
            self.walletProgressPage.pageFrame.updateProgress(
               f"setting up master record (id: {notif.initFile})")
         elif notif.which() == 'readFile':
            self.walletProgressPage.pageFrame.updateProgress("populating master record")
         elif notif.which() == 'createAccount':
            self.walletProgressPage.pageFrame.updateProgress(
               f"adding account: {notif.createAccount}")
         elif notif.which() == 'extendChain':
            chainProg = notif.extendChain
            self.walletProgressPage.pageFrame.updateProgress("extending address chain: "
               f"{chainProg.current}/{chainProg.total}")
         return

   ####
   def parseProtoPacket(self, payload):
      """Parse protocol packets from the backend."""
      TheSignalExecution.executeMethod(self.processCallback, payload)

   ####
   def proceedWithMigration(self):
      """
      Ask bridge to migrate the wallet, notifications will be sent
      to overloaded parseProtoPacket (from ServerPush parent class)
      """
      # Mark migration as started
      self.migrationStarted = True

      def wrapperFunc(capnReply):
         if capnReply.success == False:
            self.migrationFailed = True
         else:
            self.migrationComplete = True

      TheBridge.wltManager.migrateWallet(
         self.walletPath,
         self.callbackId, wrapperFunc)

   ####
   def reject(self):
      """Handle dialog rejection (cancel button or close)."""
      if self.migrationComplete:
         self.accept()
      elif not self.migrationStarted:
         # Migration hasn't started yet (Page 1) - just close dialog
         super().reject()
      else:
         # Migration has started but not completed - send cancellation signal
         self.migrationFailed = True
         if not self.cancellationSent:
            packet = self.getNewPacket()
            packet.success = False
            self.reply()
            self.cancellationSent = True

   ####
   def promptPassphraseReuseChoice(self):
      """
      Show a modal dialog asking the user to reuse or create a new passphrase.
      """
      msgBox = QtWidgets.QMessageBox(self)
      msgBox.setWindowTitle(self.tr('Passphrase Options'))
      msgBox.setText(self.tr(
         'Would you like to reuse your current passphrase'
         ' or set a new one for the migrated wallet?'))
      reuseBtn = msgBox.addButton(self.tr(
         'Reuse Passphrase'), QtWidgets.QMessageBox.AcceptRole)
      newBtn = msgBox.addButton(self.tr(
         'Set New Passphrase'), QtWidgets.QMessageBox.ActionRole)
      cancelBtn = msgBox.addButton(self.tr(
         'Cancel'), QtWidgets.QMessageBox.RejectRole)
      msgBox.setDefaultButton(reuseBtn)
      msgBox.exec_()
      if msgBox.clickedButton() == reuseBtn:
         return 'reuse'
      elif msgBox.clickedButton() == newBtn:
         return 'new'
      else:
         return 'cancel'

   ####
   def nextFromSetPassphrase(self):
      """Called when setPassphrasePage is complete."""
      self.newPassphrase = self.setPassphrasePage.pageFrame.getPassphrase()
      self.verifyPassphrasePage.pageFrame.edtPasswd3.clear()
      self.stack.setCurrentWidget(self.verifyPassphrasePage)
      self.resize(self.stack.currentWidget().sizeHint())

   ####
   def nextFromVerifyPassphrase(self):
      """Called when verifyPassphrasePage is complete."""
      verifyPass = self.verifyPassphrasePage.pageFrame.edtPasswd3.text()
      if verifyPass != self.newPassphrase:
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid Passphrase'),
            self.tr('You entered your confirmation passphrase incorrectly!'),
            QtWidgets.QMessageBox.Ok)
         return

      # Passphrase verification successful, now process it through the standard flow
      self.processSetNewPassphrase(True, reusePassphrase=False)
      self.navigateToProgressPage()

   ####
   def showEvent(self, event):
      """Handle the show event to connect additional signals."""
      super().showEvent(event)
      if not self.wired:
         self.wired = True
         self.setPassphrasePage.pageFrame.editPasswd2.returnPressed.connect(
            self.nextFromSetPassphrase)
         self.verifyPassphrasePage.pageFrame.edtPasswd3.returnPressed.connect(
            self.nextFromVerifyPassphrase)

   ####
   def handleUnlockCancelForWatchOnly(self):
      """
      Handle cancellation of unlock dialog for watching-only wallet creation.

      This method is the master controller for the unlock-cancellation flow.
      It returns True in all cases to signify to the caller that the logic
      has been handled and the caller's only responsibility is to close.
      """
      reply = QtWidgets.QMessageBox.question(
         self, self.tr('Create Watching-Only Wallet?'),
         self.tr(
            'Wallet was not unlocked.'
            ' Do you want to create a watching-only wallet instead?'),
         QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No
      )
      if reply == QtWidgets.QMessageBox.Yes:
         # User wants watching-only. Send successful reply with empty passphrase
         # to trigger the backend's watching-only migration path.
         packet = self.getNewPacket()
         packet.unlockRequest = ""
         packet.success = True
         self.reply()

         # Switch to the progress page to show the creation process.
         self.navigateToProgressPage()
         return True
      else:
         # User does NOT want to create a watching-only wallet.
         # Reject the entire migration.
         self.reject()
         return True

   ####
   def cancelPassphraseSetup(self):
      """Handle cancellation from passphrase setup pages."""
      # Show cancellation message
      QtWidgets.QMessageBox.critical(
         self,
         self.tr('Migration Cancelled'),
         self.tr('Wallet migration was cancelled.')
      )
      self.migrationFailed = True
      self.stack.setCurrentWidget(self.walletProgressPage)
      self.walletProgressPage.pageFrame.progressBar.hide()
      # Show failure message
      if not self.resultLabel:
         self.createFailureLabel(f"Migration of Wallet {self.walletData.walletId} failed!")
      self.doneBtn.setEnabled(True)

   ####
   def closeEvent(self, event):
      """Handle window close event to ensure proper cancellation."""
      if not self.migrationComplete:
         self.reject()
      else:
         super().closeEvent(event)
