################################################################################
#                                                                              #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                           #
# Distributed under the GNU Affero General Public License (AGPL v3)            #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                         #
#                                                                              #
# Copyright (C) 2016-2025, goatpig                                             #
#  Distributed under the MIT license                                           #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                      #
#                                                                              #
################################################################################

from qtpy import QtCore, QtGui, QtWidgets

from armoryengine.ArmoryUtils import USE_TESTNET, USE_REGTEST, int_to_binary
from ui.WalletFrames import NewWalletFrame, SetPassphraseFrame, \
   VerifyPassphraseFrame, WalletBackupFrame, WalletProgressFrame, \
   WizardCreateWatchingOnlyWalletFrame, CardDeckFrame
from ui.TxFrames import SendBitcoinsFrame
from ui.TxFramesOffline import SignBroadcastOfflineTxFrame
from ui.QtExecuteSignal import TheSignalExecution
from armoryengine.PyBtcWallet import PyBtcWallet
from armoryengine.BDM import TheBDM, BDM_OFFLINE, BDM_UNINITIALIZED
from armoryengine.CppBridge import ServerPush

from qtdialogs.qtdefines import AddToRunningDialogsList, \
   USERMODE, GETFONT, MSGBOX
from qtdialogs.DlgOfflineTx import ReviewOfflineTxFrame
from qtdialogs.MsgBoxCustom import MsgBoxCustom

# This class is intended to be an abstract Wizard class that
# will hold all of the functionality that is common to all
# Wizards in Armory.
class ArmoryWizard(QtWidgets.QWizard):
   def __init__(self, parent, main):
      super(ArmoryWizard, self).__init__(parent)
      self.setWizardStyle(QtWidgets.QWizard.ClassicStyle)
      self.parent = parent
      self.main = main
      self.setFont(GETFONT('var'))
      self.setWindowFlags(QtCore.Qt.Window)

      # Need to adjust the wizard frame size whenever the page changes.
      self.currentIdChanged.connect(self.fitContents)
      if USE_TESTNET:
         self.setWindowTitle('Armory - Bitcoin Wallet Management [TESTNET]')
         self.setWindowIcon(QtGui.QIcon('img/armory_icon_green_32x32.png'))
      elif USE_REGTEST:
         self.setWindowTitle('Armory - Bitcoin Wallet Management [REGTEST]')
         self.setWindowIcon(QtGui.QIcon('img/armory_icon_green_32x32.png'))
      else:
         self.setWindowTitle('Armory - Bitcoin Wallet Management')
         self.setWindowIcon(QtGui.QIcon('img/armory_icon_32x32.png'))

   def fitContents(self):
      self.adjustSize()

   @AddToRunningDialogsList
   def exec_(self):
      return super(ArmoryWizard, self).exec_()

# This class is intended to be an abstract Wizard Page class that
# will hold all of the functionality that is common to all
# Wizard pages in Armory.
# The layout is QtWidgets.QVBoxLayout and holds a single QtWidgets.QFrame (self.pageFrame)
class ArmoryWizardPage(QtWidgets.QWizardPage):
   def __init__(self, wizard, pageFrame):
      super(ArmoryWizardPage, self).__init__(wizard)
      self.pageFrame = pageFrame
      self.pageLayout = QtWidgets.QVBoxLayout()
      self.pageLayout.addWidget(self.pageFrame)
      self.setLayout(self.pageLayout)

   # override this method to implement validators
   def validatePage(self):
      return True

################################################################################
class CreateWalletNotifHandler(ServerPush):
   def __init__(self, page):
      ServerPush.__init__(self)
      self.wizard = page.wizard
      self.frame = page.pageFrame

   def replyWithPassphrase(self,
      targetMs, targetMB, passphrase):
      packet = self.getNewPacket()
      reply = packet.init('setPassphrase')
      reply.passphrase = passphrase
      if targetMB > 0:
         reply.kdfTargetMB = targetMB
      else:
         reply.kdfTargetMs = targetMs
      packet.success = True
      self.reply()

   def parseProtoPacket(self, payload):
      if payload.which() == 'cleanup':
         TheBDM.unregisterPrompt(self.callbackId)
         self.frame.setDone()
         return

      elif payload.which() == 'setPassphrase':
         notif = payload.setPassphrase
         if notif.which() == 'controlPass':
            self.wizard.setControlPassphrase(self.replyWithPassphrase)
         elif notif.which() == 'privatePass':
            self.wizard.setPrivatePassphrase(self.replyWithPassphrase)
         else:
            return

      elif payload.which() == 'walletProgress':
         notif = payload.walletProgress
         if notif.which() == 'createFile':
            self.frame.updateProgress(f"creating file: {notif.createFile}")
         elif notif.which() == 'initFile':
            self.frame.updateProgress(
               f"setting up master record (id: {notif.initFile})")
         elif notif.which() == 'readFile':
            self.frame.updateProgress("populating master record")
         elif notif.which() == 'createAccount':
            self.frame.updateProgress(f"adding account: {notif.createAccount}")
         elif notif.which() == 'extendChain':
            chainProg = notif.extendChain
            self.frame.updateProgress("extending address chain: "
               f"{chainProg.current}/{chainProg.total}")
         else:
            return

################################ Wallet Wizard #################################
# Wallet Wizard has these pages:
#     1. Create Wallet
#     2. Set Passphrase
#     3. Verify Passphrase
#     4. Create Paper Backup
#     5. Create Watching Only Wallet
class WalletWizard(ArmoryWizard):
   def __init__(self, parent, main):
      super(WalletWizard,self).__init__(parent, main)
      self.newWallet = None
      self.isBackupCreated = False
      self.setWindowTitle(self.tr("Wallet Creation Wizard"))
      self.setOption(QtWidgets.QWizard.HaveFinishButtonOnEarlyPages, on=True)
      self.setOption(QtWidgets.QWizard.IgnoreSubTitles, on=True)
      self.passphrase = None
      self.reuse_passphrase = False  # New flag for migration reuse

      self.walletCreationId, \
         self.manualEntropyId, \
         self.setPassphraseId, \
         self.verifyPassphraseId, \
         self.walletProgressId, \
         self.walletBackupId, \
         self.WOWId = range(7)

      # Page 1: Create Wallet
      self.walletCreationPage = WalletCreationPage(self)
      self.setPage(self.walletCreationId, self.walletCreationPage)

      # Page 1.5: Add manual entropy
      self.manualEntropyPage = ManualEntropyPage(self)
      self.setPage(self.manualEntropyId, self.manualEntropyPage)

      # Page 2: Set Passphrase
      self.setPassphrasePage = SetPassphrasePage(self)
      self.setPage(self.setPassphraseId, self.setPassphrasePage)

      # Page 3: Verify Passphrase
      self.verifyPassphrasePage = VerifyPassphrasePage(self)
      self.setPage(self.verifyPassphraseId, self.verifyPassphrasePage)

      # Page 4: Wallet creation progress
      self.walletProgressPage = WalletProgressPage(self)
      self.setPage(self.walletProgressId, self.walletProgressPage)

      # Page 5: Create Paper Backup
      self.walletBackupPage = WalletBackupPage(self)
      self.setPage(self.walletBackupId, self.walletBackupPage)

      # Page 6: Create Watching Only Wallet -- but only if expert, or offline
      self.hasCWOWPage = False
      if self.main.usermode==USERMODE.Expert or TheBDM.getState() == BDM_OFFLINE:
         self.hasCWOWPage = True
         self.createWOWPage = CreateWatchingOnlyWalletPage(self)
         self.setPage(self.WOWId, self.createWOWPage)

      self.setButtonLayout([
         QtWidgets.QWizard.BackButton,
         QtWidgets.QWizard.Stretch,
         QtWidgets.QWizard.NextButton,
         QtWidgets.QWizard.FinishButton
      ])

   def initializePage(self, *args, **kwargs):
      if getattr(self, 'reuse_passphrase', False):
         if self.currentPage() == self.setPassphrasePage:
            pf1 = self.setPassphrasePage.pageFrame.editPasswd1.text()
            pf2 = self.setPassphrasePage.pageFrame.editPasswd2.text()
            if pf1 and pf2:
               # Set the internal passphrase value for the verify page
               self.verifyPassphrasePage.setPassphrase(pf1)
               QtCore.QTimer.singleShot(0, self.next)
         elif self.currentPage() == self.verifyPassphrasePage:
            pf3 = self.verifyPassphrasePage.pageFrame.edtPasswd3.text()
            if pf3:
               self.verifyPassphrasePage.setPassphrase(pf3)
               QtCore.QTimer.singleShot(0, self.next)
      elif self.currentPage() == self.verifyPassphrasePage:
         self.verifyPassphrasePage.setPassphrase(
            self.setPassphrasePage.pageFrame.getPassphrase())
      elif self.hasCWOWPage and self.currentPage() == self.createWOWPage:
         self.createWOWPage.pageFrame.setWallet(self.newWallet)

      if self.currentPage() == self.walletProgressPage:
         # Hide the back button starting the wallet progress page
         self.setButtonLayout([
            QtWidgets.QWizard.Stretch,
            QtWidgets.QWizard.NextButton,
            QtWidgets.QWizard.FinishButton
         ])

         # Disable Next button until wallet is created
         self.button(QtWidgets.QWizard.NextButton).setEnabled(False)

         # Create the wallet
         self.createNewWalletFromWizard()

      elif self.currentPage() == self.walletBackupPage:
         self.walletBackupPage.pageFrame.setPassphrase(self.passphrase)
         self.passphrase = None
         self.walletBackupPage.pageFrame.setWallet(self.newWallet)

      elif self.currentPage() == self.walletCreationPage:
         # Hide the back button on the first page
         self.setButtonLayout([
            QtWidgets.QWizard.Stretch,
            QtWidgets.QWizard.NextButton,
            QtWidgets.QWizard.FinishButton
         ])
      else:
         self.setButtonLayout([
            QtWidgets.QWizard.BackButton,
            QtWidgets.QWizard.Stretch,
            QtWidgets.QWizard.NextButton,
            QtWidgets.QWizard.FinishButton
         ])

   def done(self, event):
      if self.newWallet and not self.walletBackupPage.pageFrame.isBackupCreated:
         reply = QtWidgets.QMessageBox.question(self, self.tr('Wallet Backup Warning'), self.tr('<qt>'
            'You have not made a backup for your new wallet.  You only have '
            'to make a backup of your wallet <u>one time</u> to protect '
            'all the funds held by this wallet <i>any time in the future</i> '
            '(it is a backup of the signing keys, not the coins themselves).'
            '<br><br>'
            'If you do not make a backup, you will <u>permanently</u> lose '
            'the money in this wallet if you ever forget your password, or '
            'suffer from hardware failure.'
            '<br><br>'
            'Are you sure that you want to leave this wizard without backing '
            'up your wallet?</qt>'), \
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)
         if reply == QtWidgets.QMessageBox.No:
            # Stay in the wizard
            return None
      return super(WalletWizard, self).done(event)

   def setControlPassphrase(self, callback):
      callback(250, 0, "")

   def setPrivatePassphrase(self, callback):
      #i hate python
      self.passphrase = self.verifyPassphrasePage.pageFrame.getPassphrase()
      kdfMs = int(self.walletCreationPage.pageFrame.getKdfSec() * 1000)
      kdfMB = int(self.walletCreationPage.pageFrame.getKdfBytes() / 1024**2)
      callback(kdfMs, kdfMB, self.passphrase)

   def createNewWalletFromWizard(self):
      entropy = None
      if self.walletCreationPage.isManualEntropy():
         entropy = self.manualEntropyPage.pageFrame.getEntropy()
      else:
         entropy = self.main.getExtraEntropyForKeyGen()

      def finalizeInner(reply):
         if reply.success == False:
            LOGDEBUG(f"create wallet failed with error: {reply.error}")
            self.reject()
         else:
            wltId = reply.utils.createWallet
            self.newWallet = PyBtcWallet().loadFromBridge(wltId)
            self.main.addWalletToApplication(self.newWallet, walletIsNew=True)

      def finalizeCb(reply):
         TheSignalExecution.executeMethod(finalizeInner, reply)

      handler = CreateWalletNotifHandler(self.walletProgressPage)
      PyBtcWallet().createNewWallet(
         replyCallback=finalizeCb, callbackId=handler.callbackId,
         shortLabel=self.walletCreationPage.pageFrame.getName(),
         longLabel=self.walletCreationPage.pageFrame.getDescription(),
         extraEntropy=entropy)

   def cleanupPage(self, *args, **kwargs):
      if self.hasCWOWPage and self.currentPage() == self.createWOWPage:
         self.setButtonLayout([
            QtWidgets.QWizard.Stretch,
            QtWidgets.QWizard.NextButton,
            QtWidgets.QWizard.FinishButton
         ])
      # If we are backing up from setPassphrasePage must be going
      # to the first page.
      elif self.currentPage() == self.setPassphrasePage:
         # Hide the back button on the first page
         self.setButtonLayout([
            QtWidgets.QWizard.Stretch,
            QtWidgets.QWizard.NextButton,
            QtWidgets.QWizard.FinishButton
         ])
      else:
         self.setButtonLayout([
            QtWidgets.QWizard.BackButton,
            QtWidgets.QWizard.Stretch,
            QtWidgets.QWizard.NextButton,
            QtWidgets.QWizard.FinishButton
         ])

################################################################################
class ManualEntropyPage(ArmoryWizardPage):
   def __init__(self, wizard):
      super(ManualEntropyPage, self).__init__(wizard,
         CardDeckFrame(wizard, wizard.main, wizard.tr("Shuffle a deck of cards")))
      self.wizard = wizard
      self.setTitle(wizard.tr("Step 1: Add Manual Entropy"))
      self.setSubTitle(wizard.tr('Use a deck of cards to get a new random number for your wallet.'))

   def validatePage(self):
      isReady = self.pageFrame.hasGoodEntropy()
      if not isReady:
         MsgBoxCustom(MSGBOX.Info,
            title="Not enough entropy",
            msg="Pick at least 39 cards to progress further")
         return False
      return True

   def nextId(self):
      return self.wizard.setPassphraseId

########
class WalletCreationPage(ArmoryWizardPage):
   def __init__(self, wizard):
      super(WalletCreationPage, self).__init__(wizard,
         NewWalletFrame(wizard, wizard.main, "Primary Wallet"))
      self.wizard = wizard
      self.setTitle(wizard.tr("Step 1: Create Wallet"))
      self.setSubTitle(wizard.tr(
         'Create a new wallet for managing your funds. '
         'The name and description can be changed at any time.'))

   def validatePage(self):
      result = True
      if self.pageFrame.getKdfSec() == -1:
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid Target Compute Time'), \
            self.tr('You entered Target Compute Time incorrectly.\n\nEnter: <Number> (ms, s)'), QtWidgets.QMessageBox.Ok)
         result = False
      elif self.pageFrame.getKdfBytes() == -1:
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid Max Memory Usage'), \
            self.tr('You entered Max Memory Usage incorrectly.\n\nEnter: <Number> (kb, mb)'), QtWidgets.QMessageBox.Ok)
         result = False
      return result

   def isManualEntropy(self):
      return self.pageFrame.getManualEntropy()

   def nextId(self):
      if self.isManualEntropy():
         return self.wizard.manualEntropyId
      else:
         return self.wizard.setPassphraseId

########
class SetPassphrasePage(ArmoryWizardPage):
   def __init__(self, wizard):
      super(SetPassphrasePage, self).__init__(wizard,
         SetPassphraseFrame(wizard, wizard.main, wizard.tr("Set Passphrase"), self.updateNextButton))
      self.wizard = wizard
      self.setTitle(wizard.tr("Step 2: Set Passphrase"))
      self.updateNextButton()

   def updateNextButton(self):
      self.completeChanged.emit()

   def isComplete(self):
      return self.pageFrame.checkPassphrase(False)

   def nextId(self):
      if getattr(self.wizard, 'reuse_passphrase', False):
         # Skip verify passphrase page
         return self.wizard.walletProgressId
      return self.wizard.verifyPassphraseId

########
class VerifyPassphrasePage(ArmoryWizardPage):
   def __init__(self, wizard):
      super(VerifyPassphrasePage, self).__init__(wizard,
         VerifyPassphraseFrame(wizard, wizard.main, wizard.tr("Verify Passphrase")))
      self.wizard = wizard
      self.passphrase = None
      self.setTitle(wizard.tr("Step 3: Verify Passphrase"))

   def setPassphrase(self, passphrase):
      self.passphrase = passphrase

   def validatePage(self):
      result = self.passphrase == str(self.pageFrame.edtPasswd3.text())
      if not result:
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid Passphrase'), \
            self.tr('You entered your confirmation passphrase incorrectly!'), QtWidgets.QMessageBox.Ok)
      return result

   def nextId(self):
      if getattr(self.wizard, 'reuse_passphrase', False):
         # Skip directly to progress page
         return self.wizard.walletProgressId
      return self.wizard.walletProgressId

########
class WalletProgressPage(ArmoryWizardPage):
   def __init__(self, wizard):
      super(WalletProgressPage, self).__init__(wizard,
         WalletProgressFrame(wizard, wizard.main, wizard.tr("Wallet Creation Process")))
      self.wizard = wizard
      self.setTitle(wizard.tr("Step 4: Creating Wallet"))

   def validatePage(self):
      return self.pageFrame.isDone

   def nextId(self):
      return self.wizard.walletBackupId

########
class WalletBackupPage(ArmoryWizardPage):
   def __init__(self, wizard):
      super(WalletBackupPage, self).__init__(wizard,
         WalletBackupFrame(wizard, wizard.main, wizard.tr("Backup Wallet")))
      self.wizard = wizard
      self.myWizard = wizard
      self.setTitle(wizard.tr("Step 5: Backup Wallet"))
      self.setFinalPage(True)

   def nextId(self):
      if self.wizard.hasCWOWPage:
         return self.wizard.WOWId
      else:
         return -1

########
class CreateWatchingOnlyWalletPage(ArmoryWizardPage):
   def __init__(self, wizard):
      super(CreateWatchingOnlyWalletPage, self).__init__(wizard,
         WizardCreateWatchingOnlyWalletFrame(wizard, wizard.main, wizard.tr("Create Watching-Only Wallet")))
      self.wizard = wizard
      self.setTitle(wizard.tr("Step 6: Create Watching-Only Wallet"))

   def nextId(self):
      return -1

############################### Offline TX Wizard ##############################
# Offline TX Wizard has these pages:
#     1. Create Transaction
#     2. Sign Transaction on Offline Computer
#     3. Broadcast Transaction
class TxWizard(ArmoryWizard):
   def __init__(self, parent, main, wlt, prefill=None, onlyOfflineWallets=False):
      super(TxWizard,self).__init__(parent, main)
      self.setWindowTitle(self.tr("Offline Transaction Wizard"))
      self.setOption(QtWidgets.QWizard.IgnoreSubTitles, on=True)
      self.setOption(QtWidgets.QWizard.HaveCustomButton1, on=True)
      self.setOption(QtWidgets.QWizard.HaveFinishButtonOnEarlyPages, on=True)

      # Page 1: Create Offline TX
      self.createTxPage = CreateTxPage(self, wlt, prefill, onlyOfflineWallets=onlyOfflineWallets)
      self.addPage(self.createTxPage)

      # Page 2: Sign Offline TX
      self.reviewOfflineTxPage = ReviewOfflineTxPage(self)
      self.addPage(self.reviewOfflineTxPage)

      # Page 3: Broadcast Offline TX
      self.signBroadcastOfflineTxPage = SignBroadcastOfflineTxPage(self)
      self.addPage(self.signBroadcastOfflineTxPage)

      self.setButtonText(QtWidgets.QWizard.NextButton, self.tr('Create Unsigned Transaction'))
      self.setButtonText(QtWidgets.QWizard.CustomButton1, self.tr('Send!'))
      self.customButtonClicked.connect(self.sendClicked)
      self.setButtonLayout([
         QtWidgets.QWizard.CancelButton,
         QtWidgets.QWizard.BackButton,
         QtWidgets.QWizard.Stretch,
         QtWidgets.QWizard.NextButton,
         QtWidgets.QWizard.CustomButton1
      ])

   def initializePage(self, *args, **kwargs):
      if self.currentPage() == self.createTxPage:
         self.createTxPage.pageFrame.fireWalletChange()
      elif self.currentPage() == self.reviewOfflineTxPage:
         self.setButtonText(QtWidgets.QWizard.NextButton, self.tr('Next'))
         self.setButtonLayout([
            QtWidgets.QWizard.BackButton,
            QtWidgets.QWizard.Stretch,
            QtWidgets.QWizard.NextButton,
            QtWidgets.QWizard.FinishButton
         ])
         self.reviewOfflineTxPage.pageFrame.setTxDp(self.createTxPage.txdp)
         self.reviewOfflineTxPage.pageFrame.setWallet(
            self.createTxPage.pageFrame.wlt)

   def cleanupPage(self, *args, **kwargs):
      if self.currentPage() == self.reviewOfflineTxPage:
         self.updateOnSelectWallet(self.createTxPage.pageFrame.wlt)
         self.setButtonText(QtWidgets.QWizard.NextButton, self.tr('Create Unsigned Transaction'))

   def sendClicked(self, customButtonIndex):
      self.createTxPage.pageFrame.createTxAndBroadcast()
      self.accept()

   def updateOnSelectWallet(self, wlt):
      if wlt.watchingOnly:
         self.setButtonLayout([
            QtWidgets.QWizard.CancelButton,
            QtWidgets.QWizard.BackButton,
            QtWidgets.QWizard.Stretch,
            QtWidgets.QWizard.NextButton
         ])
      else:
         self.setButtonLayout([
            QtWidgets.QWizard.CancelButton,
            QtWidgets.QWizard.BackButton,
            QtWidgets.QWizard.Stretch,
            QtWidgets.QWizard.NextButton,
            QtWidgets.QWizard.CustomButton1
         ])

class CreateTxPage(ArmoryWizardPage):
   def __init__(self, wizard, wlt, prefill=None, onlyOfflineWallets=False):
      super(CreateTxPage, self).__init__(wizard,
         SendBitcoinsFrame(wizard, wizard.main,
            wizard.tr("Create Transaction"), wlt, prefill,
            selectWltCallback=self.updateOnSelectWallet,
            onlyOfflineWallets=onlyOfflineWallets))
      self.setTitle(self.tr("Step 1: Create Transaction"))
      self.txdp = None

   def validatePage(self):
      result = self.pageFrame.validateInputsGetTxDP()
      # the validator also computes the transaction and returns it or False if not valid
      if result:
         self.txdp = result
         result = True
      return result

   def updateOnSelectWallet(self, wlt):
      self.wizard().updateOnSelectWallet(wlt)

class ReviewOfflineTxPage(ArmoryWizardPage):
   def __init__(self, wizard):
      super(ReviewOfflineTxPage, self).__init__(wizard,
         ReviewOfflineTxFrame(wizard, wizard.main, self.tr("Review Offline Transaction")))
      self.setTitle(self.tr("Step 2: Review Offline Transaction"))
      self.setFinalPage(True)

class SignBroadcastOfflineTxPage(ArmoryWizardPage):
   def __init__(self, wizard):
      super(SignBroadcastOfflineTxPage, self).__init__(wizard,
         SignBroadcastOfflineTxFrame(wizard, wizard.main, self.tr("Sign/Broadcast Offline Transaction")))
      self.setTitle(self.tr("Step 3: Sign/Broadcast Offline Transaction"))