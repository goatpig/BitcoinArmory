##############################################################################
#                                                                            #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                         #
# Distributed under the GNU Affero General Public License (AGPL v3)          #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                       #
#                                                                            #
# Copyright (C) 2016-2024, goatpig                                           #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################

from qtpy import QtWidgets

from armoryengine.CppBridge import TheBridge, ServerPush
from armoryengine.ArmoryUtils import LOGERROR, LOGWARN, \
   UINT32_MAX, UINT8_MAX, UNKNOWN
from armoryengine.BDM import TheBDM
from armoryengine.PyBtcWallet import PyBtcWallet
from armoryengine.AddressUtils import binary_to_base58
from ui.QtExecuteSignal import TheSignalExecution

from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.DlgChangePassphrase import DlgChangePassphrase
from qtdialogs.DlgReplaceWallet import DlgReplaceWallet
from qtdialogs.MsgBoxCustom import MsgBoxCustom
from qtdialogs.qtdefines import HLINE, QRichLabel, STRETCH, STYLE_RAISED, \
   makeHorizFrame, makeVertFrame, MSGBOX, GETFONT, tightSizeStr, \
   AdvancedOptionsFrame


################################################################################
# Create a special QtWidgets.QLineEdit with a masked input
# Forces the cursor to start at position 0 whenever there is no input
class MaskedInputLineEdit(QtWidgets.QLineEdit):
   def __init__(self, inputMask):
      super(MaskedInputLineEdit, self).__init__()
      self.setInputMask(inputMask)
      fixFont = GETFONT('Fix', 9)
      self.setFont(fixFont)
      self.setMinimumWidth(tightSizeStr(fixFont, inputMask)[0] + 10)
      self.cursorPositionChanged.connect(self.controlCursor)

   def controlCursor(self, oldpos, newpos):
      if newpos != 0 and len(str(self.text()).strip()) == 0:
         self.setCursorPosition(0)


################################################################################
class DlgRestoreSingle(ArmoryDialog, ServerPush):
   def __init__(self, parent, main, thisIsATest=False, expectWltID=None):
      ServerPush.__init__(self)
      ArmoryDialog.__init__(self, parent, main)

      self.newWltID = None
      self.thisIsATest = thisIsATest
      self.testWltID = expectWltID
      if thisIsATest:
         lblDescr = QRichLabel(self.tr(
            '<b><u><font color="blue" size="4">Test a Paper Backup</font></u></b> '
            '<br><br>'
            'Use this window to test a single-sheet paper backup.  If your '
            'backup includes imported keys, those will not be covered by this test.'))
      else:
         lblDescr = QRichLabel(self.tr(
            '<b><u>Restore a Wallet from Paper Backup</u></b> '
            '<br><br>'
            'Use this window to restore a single-sheet paper backup. '
            'If your backup includes extra pages with '
            'imported keys, please restore the base wallet first, then '
            'double-click the restored wallet and select "Import Private '
            'Keys" from the right-hand menu.'))

      lblType = QRichLabel(self.tr('<b>Backup Type:</b>'), doWrap=False)
      self.version135Button = QtWidgets.QRadioButton(self.tr('Version 1.35 (4 lines)'), self)
      self.version135aButton = QtWidgets.QRadioButton(self.tr('Version 1.35a (4 lines Unencrypted)'), self)
      self.version135aSPButton = QtWidgets.QRadioButton(self.tr(u'Version 1.35a (4 lines + SecurePrint\u200b\u2122)'), self)
      self.version135cButton = QtWidgets.QRadioButton(self.tr('Version 1.35c (2 lines Unencrypted)'), self)
      self.version135cSPButton = QtWidgets.QRadioButton(self.tr(u'Version 1.35c (2 lines + SecurePrint\u200b\u2122)'), self)
      self.backupTypeButtonGroup = QtWidgets.QButtonGroup(self)
      self.backupTypeButtonGroup.addButton(self.version135Button)
      self.backupTypeButtonGroup.addButton(self.version135aButton)
      self.backupTypeButtonGroup.addButton(self.version135aSPButton)
      self.backupTypeButtonGroup.addButton(self.version135cButton)
      self.backupTypeButtonGroup.addButton(self.version135cSPButton)
      self.version135cButton.setChecked(True)
      self.backupTypeButtonGroup.buttonClicked.connect(self.changeType)

      layoutRadio = QtWidgets.QVBoxLayout()
      layoutRadio.addWidget(self.version135Button)
      layoutRadio.addWidget(self.version135aButton)
      layoutRadio.addWidget(self.version135aSPButton)
      layoutRadio.addWidget(self.version135cButton)
      layoutRadio.addWidget(self.version135cSPButton)
      layoutRadio.setSpacing(0)

      radioButtonFrame = QtWidgets.QFrame()
      radioButtonFrame.setLayout(layoutRadio)

      frmBackupType = makeVertFrame([lblType, radioButtonFrame])

      self.lblSP = QRichLabel(self.tr(u'SecurePrint\u200b\u2122 Code:'), doWrap=False)
      self.editSecurePrint = QtWidgets.QLineEdit()
      self.prfxList = [QtWidgets.QLabel(self.tr('Root Key:')), QtWidgets.QLabel(''), QtWidgets.QLabel(self.tr('Chaincode:')), QtWidgets.QLabel('')]

      inpMask = '<AAAA\ AAAA\ AAAA\ AAAA\ \ AAAA\ AAAA\ AAAA\ AAAA\ \ AAAA!'
      self.edtList = [MaskedInputLineEdit(inpMask) for i in range(4)]
      self.frmSP = makeHorizFrame([STRETCH, self.lblSP, self.editSecurePrint])

      frmAllInputs = QtWidgets.QFrame()
      frmAllInputs.setFrameStyle(STYLE_RAISED)
      layoutAllInp = QtWidgets.QGridLayout()
      layoutAllInp.addWidget(self.frmSP, 0, 0, 1, 2)
      for i in range(4):
         layoutAllInp.addWidget(self.prfxList[i], i + 1, 0)
         layoutAllInp.addWidget(self.edtList[i], i + 1, 1)
      frmAllInputs.setLayout(layoutAllInp)

      doItText = self.tr('Test Backup') if thisIsATest else self.tr('Restore Wallet')

      self.btnAccept = QtWidgets.QPushButton(doItText)
      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.btnAccept.clicked.connect(self.verifyUserInput)
      self.btnCancel.clicked.connect(self.reject)
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)

      self.chkEncrypt = QtWidgets.QCheckBox(self.tr('Encrypt Wallet'))
      self.chkEncrypt.setChecked(True)
      bottomFrm = makeHorizFrame([self.chkEncrypt, buttonBox])

      walletRestoreTabs = QtWidgets.QTabWidget()
      backupTypeFrame = makeVertFrame([frmBackupType, frmAllInputs])
      walletRestoreTabs.addTab(backupTypeFrame, self.tr("Backup"))
      self.advancedOptionsTab = AdvancedOptionsFrame(parent, main)
      walletRestoreTabs.addTab(self.advancedOptionsTab, self.tr("Advanced Options"))

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblDescr)
      layout.addWidget(HLINE())
      layout.addWidget(walletRestoreTabs)
      layout.addWidget(bottomFrm)
      self.setLayout(layout)

      self.chkEncrypt.setChecked(not thisIsATest)
      self.chkEncrypt.setVisible(not thisIsATest)
      self.advancedOptionsTab.setEnabled(not thisIsATest)
      if thisIsATest:
         self.setWindowTitle(self.tr('Test Single-Sheet Backup'))
      else:
         self.setWindowTitle(self.tr('Restore Single-Sheet Backup'))
         self.chkEncrypt.clicked.connect(self.onEncryptCheckboxChange)

      self.setMinimumWidth(500)
      self.layout().setSizeConstraint(QtWidgets.QLayout.SetFixedSize)
      self.changeType(self.backupTypeButtonGroup.checkedButton())

   #############################################################################
   # Hide advanced options whenver the restored wallet is unencrypted
   def onEncryptCheckboxChange(self):
      self.advancedOptionsTab.setEnabled(self.chkEncrypt.isChecked())

   #############################################################################
   def accept(self):
      TheBDM.unregisterPrompt(self.callbackId)
      super(ArmoryDialog, self).accept()

   #############################################################################
   def reject(self):
      TheBDM.unregisterPrompt(self.callbackId)
      super(ArmoryDialog, self).reject()

   #############################################################################
   def changeType(self, sel):
      if   sel == self.version135Button:
         visList = [0, 1, 1, 1, 1]
      elif sel == self.version135aButton:
         visList = [0, 1, 1, 1, 1]
      elif sel == self.version135aSPButton:
         visList = [1, 1, 1, 1, 1]
      elif sel == self.version135cButton:
         visList = [0, 1, 1, 0, 0]
      elif sel == self.version135cSPButton:
         visList = [1, 1, 1, 0, 0]
      else:
         LOGERROR('What the heck backup type is selected?  %d', sel)
         return

      self.doMask = (visList[0] == 1)
      self.frmSP.setVisible(self.doMask)
      for i in range(4):
         self.prfxList[i].setVisible(visList[i + 1] == 1)
         self.edtList[ i].setVisible(visList[i + 1] == 1)

      self.isLongForm = (visList[-1] == 1)

   #############################################################################
   def flagEditLine(self, lineId):
      self.edtList[lineId].setStyleSheet(
         '''
         QLineEdit {
            color: rgb(180, 0, 0)
         }
         '''
      )

   def resetEditLines(self):
      for edtLine in self.edtList:
         edtLine.setStyleSheet(
         '''
         QLineEdit {
            color: rgb(0, 0, 0)
         }
         '''
      )

   #############################################################################
   def processCallback(self, payload):
      if payload.which() != 'restore':
         #unexpected callback type
         LOGERROR(f"unexpected backup restore callback which: {payload.which()}")
         self.reject()
         return

      restorePayload = payload.restore
      which = restorePayload.which()
      if which == 'checkWalletId':
         newWltID = restorePayload.checkWalletId.walletId
         if not newWltID:
            LOGWARN("empty wallet id in backup restore process")

         '''
         #TODO: also check/test backup type
         wltType = restorePayload.checkWalletId.backupType
         '''

         replyToBridge = self.getNewPacket()
         replyToBridge.success = False

         #ask the user to check the restored wallet id
         userAccept = QtWidgets.QMessageBox.question(self, self.tr('Verify Wallet ID'),
            self.tr("The data you entered corresponds to a wallet with a wallet ID:"
            f"\n\n{newWltID}\n\n"
            'Does this ID match the "Wallet Unique ID" printed on your paper backup?'
            'If not, click "No" and reenter key and chain-code data again.'),
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)

         if userAccept == QtWidgets.QMessageBox.Yes:
            if self.thisIsATest:
               #stop here if this was just a test
               verifyRecoveryTestID(self, newWltID, self.testWltID)
            else:
               self.newWltID = newWltID
               replyToBridge.success = True

               #check if this wallet is already loaded
               dlgOwnWlt = None
               if self.main.wallets.hasWallet(newWltID):
                  dlgOwnWlt = DlgReplaceWallet(newWltID, self.parent, self.main)
                  if (dlgOwnWlt.exec_()):
                     #should we merge with the old wallet or overwrite it?
                     if dlgOwnWlt.output == 2:
                        replyToBridge.restore = 'merge'
                     else:
                        replyToBridge.restore = 'overwrite'
                  else:
                     replyToBridge.success = False

         if replyToBridge.success == False:
            self.reject()
         self.reply()

      elif which == 'checksumError':
         checksums = restorePayload.checksumError
         for chkResult in checksums:
            if chkResult.value < 0 or chkResult.value == UINT8_MAX:
               self.flagEditLine(chkResult.lineId)

         reply = QtWidgets.QMessageBox.critical(self, self.tr('Invalid Data'), self.tr(
            'There is an error in the data you entered that could not be '
            'fixed automatically.  Please double-check that you entered the '
            'text exactly as it appears on the wallet-backup page.'), \
            QtWidgets.QMessageBox.Ok)
         LOGWARN('Bad input in wallet restore field')
         return

      elif which == 'checksumMismatch':
         #TODO: deal with backup type mismatch
         LOGWARN("restore backup type mismatch")
         self.reject()
         return

      elif which == 'getPassphrases':
         #returns new wallet's private keys password
         #TODO: add UI for control pass submission
         reply = self.getNewPacket()

         passwd = []
         if self.chkEncrypt.isChecked():
            dlgPasswd = DlgChangePassphrase(self, self.main)
            if dlgPasswd.exec_():
               reply.success = True
               reply.init('passphrases', 1)
               reply.passphrases[0] = str(dlgPasswd.edtPasswd1.text())
            else:
               QtWidgets.QMessageBox.critical(self, self.tr('Cannot Encrypt'), \
                  self.tr('You requested your restored wallet be encrypted, but no '
                  'valid passphrase was supplied. Aborting wallet recovery.'), \
                  QtWidgets.QMessageBox.Ok)
               reply.success = False
               self.reject()
         self.reply()

      elif which == 'success':
         if not self.newWltID:
            LOGERROR("wallet import did not yield an id")
            raise Exception("wallet import did not yield an id")

         self.newWallet = PyBtcWallet().loadFromBridge(self.newWltID)
         self.accept()

      elif which == "failure":
         QtWidgets.QMessageBox.critical(self, self.tr('Failure'), self.tr(
            f'Backup process failed with error:\n\n{restorePayload.failure}\n. Aborting.'), \
            QtWidgets.QMessageBox.Ok)
         self.reject()

      elif which == "decryptError":
         #TODO: notify of invalid SP pass
         LOGWARN("backup restore decrypt error")
         self.reject()

      elif which == "typeError":
         #TODO: wallet type conveyed by backup is unknown
         LOGWARN("backup restore type error")
         self.reject()

      else:
         #TODO: unknown error
         LOGWARN("backup restore unhandled callback which")
         self.reject()

   def parseProtoPacket(self, payload):
      TheSignalExecution.executeMethod(self.processCallback, payload)

   #############################################################################
   def verifyUserInput(self):
      #reset flagged inputs if any
      self.resetEditLines()

      root = []
      for i in range(2):
         root.append(str(self.edtList[i].text()))

      chaincode = []
      if self.isLongForm:
         for i in range(2):
            chaincode.append(str(self.edtList[i+2].text()))

      self.lineCount = len(root) + len(chaincode)

      spPass = ""
      if self.doMask:
         #add secureprint passphrase if this backup is encrypted
         spPass = str(self.editSecurePrint.text()).strip()

      '''
      verifyBackupString is a method that will trigger multiple callbacks
      during the course of its execution. Unlike a password request callback
      which only requires to generate a dedicated dialog to retrieve passwords
      from users, verifyBackupString set of notifications is complex and comes
      with branches.

      A dedicated callbackId is generated for this interaction and passed to
      TheBDM callback map along with a py side method to handle the proto
      packet from the C++ side.

      The C++ method is called with that id.
      '''
      TheBridge.restoreWallet(root, chaincode, spPass, self.callbackId)

      '''
      if self.chkEncrypt.isChecked() and self.advancedOptionsTab.getKdfSec() == -1:
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid Target Compute Time'), \
            self.tr('You entered Target Compute Time incorrectly.\n\nEnter: <Number> (ms, s)'), QtWidgets.QMessageBox.Ok)
         return
      if self.chkEncrypt.isChecked() and self.advancedOptionsTab.getKdfBytes() == -1:
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid Max Memory Usage'), \
            self.tr('You entered Max Memory Usage incorrectly.\n\nEnter: <Number> (kB, MB)'), QtWidgets.QMessageBox.Ok)
         return

      if nError > 0:
         pluralStr = 'error' if nError == 1 else 'errors'
         msg = self.tr(
            'Detected errors in the data you entered. '
            'Armory attempted to fix the errors but it is not '
            'always right.  Be sure to verify the "Wallet Unique ID" '
            'closely on the next window.')

         QtWidgets.QMessageBox.question(self, self.tr('Errors Corrected'), msg, \
            QtWidgets.QMessageBox.Ok)
      '''

################################################################################
class DlgRestoreFragged(ArmoryDialog):
   def __init__(self, parent, main, thisIsATest=False, expectWltID=None):
      super(DlgRestoreFragged, self).__init__(parent, main)

      self.thisIsATest = thisIsATest
      self.testWltID = expectWltID
      headerStr = ''
      if thisIsATest:
         headerStr = self.tr('<font color="blue" size="4">Testing a '
                      'Fragmented Backup</font>')
      else:
         headerStr = self.tr('Restore Wallet from Fragments')

      descr = self.trUtf8(
         '<b><u>%s</u></b> <br><br>'
         'Use this form to enter all the fragments to be restored.  Fragments '
         'can be stored on a mix of paper printouts, and saved files. '
         u'If any of the fragments require a SecurePrint\u200b\u2122 code, '
         'you will only have to enter it once, since that code is the same for '
         'all fragments of any given wallet.' % headerStr)

      if self.thisIsATest:
         descr += self.tr('<br><br>'
             '<b>For testing purposes, you may enter more fragments than needed '
             'and Armory will test all subsets of the entered fragments to verify '
             'that each one still recovers the wallet successfully.</b>')

      lblDescr = QRichLabel(descr)

      frmDescr = makeHorizFrame([lblDescr], STYLE_RAISED)

        # HLINE

      self.scrollFragInput = QtWidgets.QScrollArea()
      self.scrollFragInput.setWidgetResizable(True)
      self.scrollFragInput.setMinimumHeight(150)

      lblFragList = QRichLabel(self.tr('Input Fragments Below:'), doWrap=False, bold=True)
      self.btnAddFrag = QtWidgets.QPushButton(self.tr('+Frag'))
      self.btnRmFrag = QtWidgets.QPushButton(self.tr('-Frag'))
      self.btnRmFrag.setVisible(False)
      self.btnAddFrag.clicked.connect(self.addFragment)
      self.btnRmFrag.clicked.connect(self.removeFragment)
      self.chkEncrypt = QtWidgets.QCheckBox(self.tr('Encrypt Restored Wallet'))
      self.chkEncrypt.setChecked(True)
      frmAddRm = makeHorizFrame([self.chkEncrypt, STRETCH, self.btnRmFrag, self.btnAddFrag])

      self.fragDataMap = {}
      self.tableSize = 2
      self.wltType = UNKNOWN
      self.fragIDPrefix = UNKNOWN

      doItText = self.tr('Test Backup') if thisIsATest else self.tr('Restore from Fragments')

      btnExit = QtWidgets.QPushButton(self.tr('Cancel'))
      self.btnRestore = QtWidgets.QPushButton(doItText)
      btnExit.clicked.connect(self.reject)
      self.btnRestore.clicked.connect(self.processFrags)
      frmBtns = makeHorizFrame([btnExit, STRETCH, self.btnRestore])

      self.lblRightFrm = QRichLabel('', hAlign=QtCore.Qt.AlignHCenter)
      self.lblSecureStr = QRichLabel(self.trUtf8(u'SecurePrint\u200b\u2122 Code:'), \
                                     hAlign=QtCore.Qt.AlignHCenter,
                                     doWrap=False,
                                     color='TextWarn')
      self.displaySecureString = QtWidgets.QLineEdit()
      self.imgPie = QRichLabel('', hAlign=QtCore.Qt.AlignHCenter)
      self.imgPie.setMinimumWidth(96)
      self.imgPie.setMinimumHeight(96)
      self.lblReqd = QRichLabel('', hAlign=QtCore.Qt.AlignHCenter)
      self.lblWltID = QRichLabel('', doWrap=False, hAlign=QtCore.Qt.AlignHCenter)
      self.lblFragID = QRichLabel('', doWrap=False, hAlign=QtCore.Qt.AlignHCenter)
      self.lblSecureStr.setVisible(False)
      self.displaySecureString.setVisible(False)
      self.displaySecureString.setMaximumWidth(relaxedSizeNChar(self.displaySecureString, 16)[0])
        # The Secure String is now edited in DlgEnterOneFrag, It is only displayed here
      self.displaySecureString.setEnabled(False)
      frmSecPair = makeVertFrame([self.lblSecureStr, self.displaySecureString])
      frmSecCtr = makeHorizFrame([STRETCH, frmSecPair, STRETCH])

      frmWltInfo = makeVertFrame([STRETCH,
                                   self.lblRightFrm,
                                   self.imgPie,
                                   self.lblReqd,
                                   self.lblWltID,
                                   self.lblFragID,
                                   HLINE(),
                                   frmSecCtr,
                                   'Strut(200)',
                                   STRETCH], STYLE_SUNKEN)


      fragmentsLayout = QtWidgets.QGridLayout()
      fragmentsLayout.addWidget(frmDescr, 0, 0, 1, 2)
      fragmentsLayout.addWidget(frmAddRm, 1, 0, 1, 1)
      fragmentsLayout.addWidget(self.scrollFragInput, 2, 0, 1, 1)
      fragmentsLayout.addWidget(frmWltInfo, 1, 1, 2, 1)
      setLayoutStretchCols(fragmentsLayout, 1, 0)

      walletRestoreTabs = QtWidgets.QTabWidget()
      fragmentsFrame = QtWidgets.QFrame()
      fragmentsFrame.setLayout(fragmentsLayout)
      walletRestoreTabs.addTab(fragmentsFrame, self.tr("Fragments"))
      self.advancedOptionsTab = AdvancedOptionsFrame(parent, main)
      walletRestoreTabs.addTab(self.advancedOptionsTab, self.tr("Advanced Options"))

      self.chkEncrypt.setChecked(not thisIsATest)
      self.chkEncrypt.setVisible(not thisIsATest)
      self.advancedOptionsTab.setEnabled(not thisIsATest)
      if not thisIsATest:
         self.chkEncrypt.clicked.connect(self.onEncryptCheckboxChange)

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(walletRestoreTabs)
      layout.addWidget(frmBtns)
      self.setLayout(layout)
      self.setMinimumWidth(650)
      self.setMinimumHeight(500)
      self.sizeHint = lambda: QtCore.QSize(800, 650)
      self.setWindowTitle(self.tr('Restore wallet from fragments'))

      self.makeFragInputTable()
      self.checkRestoreParams()

   #############################################################################
   # Hide advanced options whenver the restored wallet is unencrypted
   def onEncryptCheckboxChange(self):
      self.advancedOptionsTab.setEnabled(self.chkEncrypt.isChecked())

   def makeFragInputTable(self, addCount=0):

      self.tableSize += addCount
      newLayout = QtWidgets.QGridLayout()
      newFrame = QtWidgets.QFrame()
      self.fragsDone = []
      newLayout.addWidget(HLINE(), 0, 0, 1, 5)
      for i in range(self.tableSize):
         btnEnter = QtWidgets.QPushButton(self.tr('Type Data'))
         btnLoad = QtWidgets.QPushButton(self.tr('Load File'))
         btnClear = QtWidgets.QPushButton(self.tr('Clear'))
         lblFragID = QRichLabel('', doWrap=False)
         lblSecure = QtWidgets.QLabel('')
         if i in self.fragDataMap:
            M, fnum, wltID, doMask, fid = ReadFragIDLineBin(self.fragDataMap[i][0])
            self.fragsDone.append(fnum)
            lblFragID.setText('<b>' + fid + '</b>')
            if doMask:
               lblFragID.setText('<b>' + fid + '</b>', color='TextWarn')


         btnEnter.clicked.connect(functools.partial(self.dataEnter, fnum=i))
         btnLoad.clicked.connect(functools.partial(self.dataLoad, fnum=i))
         btnClear.clicked.connect(functools.partial(self.dataClear, fnum=i))


         newLayout.addWidget(btnEnter, 2 * i + 1, 0)
         newLayout.addWidget(btnLoad, 2 * i + 1, 1)
         newLayout.addWidget(btnClear, 2 * i + 1, 2)
         newLayout.addWidget(lblFragID, 2 * i + 1, 3)
         newLayout.addWidget(lblSecure, 2 * i + 1, 4)
         newLayout.addWidget(HLINE(), 2 * i + 2, 0, 1, 5)

      btnFrame = QtWidgets.QFrame()
      btnFrame.setLayout(newLayout)

      frmFinal = makeVertFrame([btnFrame, STRETCH], STYLE_SUNKEN)
      self.scrollFragInput.setWidget(frmFinal)

      self.btnAddFrag.setVisible(self.tableSize < 12)
      self.btnRmFrag.setVisible(self.tableSize > 2)


   #############################################################################
   def addFragment(self):
      self.makeFragInputTable(1)

   #############################################################################
   def removeFragment(self):
      self.makeFragInputTable(-1)
      toRemove = []
      for key, val in self.fragDataMap.iteritems():
         if key >= self.tableSize:
            toRemove.append(key)

        # Have to do this in a separate loop, cause you can't remove items
        # from a map while you are iterating over them
      for key in toRemove:
         self.dataClear(key)


   #############################################################################
   def dataEnter(self, fnum):
      dlg = DlgEnterOneFrag(self, self.main, self.fragsDone, self.wltType, self.displaySecureString.text())
      if dlg.exec_():
         LOGINFO('Good data from enter_one_frag exec! %d', fnum)
         self.displaySecureString.setText(dlg.editSecurePrint.text())
         self.addFragToTable(fnum, dlg.fragData)
         self.makeFragInputTable()


   #############################################################################
   def dataLoad(self, fnum):
      LOGINFO('Loading data for entry, %d', fnum)
      toLoad = str(self.main.getFileLoad('Load Fragment File', \
                                    ['Wallet Fragments (*.frag)']))

      if len(toLoad) == 0:
         return

      if not os.path.exists(toLoad):
         LOGERROR('File just chosen does not exist! %s', toLoad)
         QtWidgets.QMessageBox.critical(self, self.tr('File Does Not Exist'), self.tr(
             'The file you select somehow does not exist...? '
             '<br><br>%s<br><br> Try a different file' % toLoad), \
             QtWidgets.QMessageBox.Ok)

      fragMap = {}
      with open(toLoad, 'r') as fin:
         allData = [line.strip() for line in fin.readlines()]
         fragMap = {}
         for line in allData:
            if line[:2].lower() in ['id', 'x1', 'x2', 'x3', 'x4', \
                                        'y1', 'y2', 'y3', 'y4', \
                                        'f1', 'f2', 'f3', 'f4']:
               fragMap[line[:2].lower()] = line[3:].strip().replace(' ', '')


      cList, nList = [], []
      if len(fragMap) == 9:
         cList, nList = ['x', 'y'], ['1', '2', '3', '4']
      elif len(fragMap) == 5:
         cList, nList = ['f'], ['1', '2', '3', '4']
      elif len(fragMap) == 3:
         cList, nList = ['f'], ['1', '2']
      else:
         LOGERROR('Unexpected number of lines in the frag file, %d', len(fragMap))
         return

      fragData = []
      fragData.append(hex_to_binary(fragMap['id']))
      for c in cList:
         for n in nList:
            mapKey = c + n
            rawBin, err = readSixteenEasyBytes(fragMap[c + n])
            if err == 'Error_2+':
               QtWidgets.QMessageBox.critical(self, self.tr('Fragment Error'), self.tr(
                  'There was an unfixable error in the fragment file: '
                  '<br><br> File: %s <br> Line: %s <br>' % (toLoad, mapKey)), \
                  QtWidgets.QMessageBox.Ok)
               return
            #fragData.append(SecureBinaryData(rawBin))
            rawBin = None

      self.addFragToTable(fnum, fragData)
      self.makeFragInputTable()


   #############################################################################
   def dataClear(self, fnum):
      if not fnum in self.fragDataMap:
         return

      for i in range(1, 3):
         self.fragDataMap[fnum][i].destroy()
      del self.fragDataMap[fnum]
      self.makeFragInputTable()
      self.checkRestoreParams()


   #############################################################################
   def checkRestoreParams(self):
      showRightFrm = False
      self.btnRestore.setEnabled(False)
      self.lblRightFrm.setText(self.tr(
         '<b>Start entering fragments into the table to left...</b>'))
      for row, data in self.fragDataMap.iteritems():
         showRightFrm = True
         M, fnum, setIDBin, doMask, idBase58 = ReadFragIDLineBin(data[0])
         self.lblRightFrm.setText(self.tr('<b><u>Wallet Being Restored:</u></b>'))
         self.imgPie.setPixmap(QtGui.QPixmap('./img/frag%df.png' % M).scaled(96,96))
         self.lblReqd.setText(self.tr('<b>Frags Needed:</b> %s' % M))
         self.lblFragID.setText(self.tr('<b>Fragments:</b> %s' % idBase58.split('-')[0]))
         self.btnRestore.setEnabled(len(self.fragDataMap) >= M)
         break

      anyMask = False
      for row, data in self.fragDataMap.iteritems():
         M, fnum, wltIDBin, doMask, idBase58 = ReadFragIDLineBin(data[0])
         if doMask:
            anyMask = True
            break
        # If all of the rows with a Mask have been removed clear the securePrintCode
      if  not anyMask:
         self.displaySecureString.setText('')
      self.lblSecureStr.setVisible(anyMask)
      self.displaySecureString.setVisible(anyMask)

      if not showRightFrm:
         self.fragIDPrefix = UNKNOWN
         self.wltType = UNKNOWN

      self.imgPie.setVisible(showRightFrm)
      self.lblReqd.setVisible(showRightFrm)
      self.lblWltID.setVisible(showRightFrm)
      self.lblFragID.setVisible(showRightFrm)


   #############################################################################
   def addFragToTable(self, tableIndex, fragData):

      if len(fragData) == 9:
         currType = '0'
      elif len(fragData) == 5:
         currType = BACKUP_TYPE_135A
      elif len(fragData) == 3:
         currType = BACKUP_TYPE_135C
      else:
         LOGERROR('How\'d we get fragData of size: %d', len(fragData))
         return

      if self.wltType == UNKNOWN:
         self.wltType = currType
      elif not self.wltType == currType:
         QtWidgets.QMessageBox.critical(self, self.tr('Mixed fragment types'), self.tr(
            'You entered a fragment for a different wallet type.  Please check '
            'that all fragments are for the same wallet, of the same version, '
            'and require the same number of fragments.'), QtWidgets.QMessageBox.Ok)
         LOGERROR('Mixing frag types!  How did that happen?')
         return


      M, fnum, wltIDBin, doMask, idBase58 = ReadFragIDLineBin(fragData[0])
        # If we don't know the Secure String Yet we have to get it
      if doMask and len(str(self.displaySecureString.text()).strip()) == 0:
         dlg = DlgEnterSecurePrintCode(self, self.main)
         if dlg.exec_():
            self.displaySecureString.setText(dlg.editSecurePrint.text())
         else:
            return

      if self.fragIDPrefix == UNKNOWN:
         self.fragIDPrefix = idBase58.split('-')[0]
      elif not self.fragIDPrefix == idBase58.split('-')[0]:
         QtWidgets.QMessageBox.critical(self, self.tr('Multiple Wallets'), self.tr(
            'The fragment you just entered is actually for a different wallet '
            'than the previous fragments you entered.  Please double-check that '
            'all the fragments you are entering belong to the same wallet and '
            'have the "number of needed fragments" (M-value, in M-of-N).'), \
            QtWidgets.QMessageBox.Ok)
         LOGERROR('Mixing fragments of different wallets! %s', idBase58)
         return


      if not self.verifyNonDuplicateFrag(fnum):
         QtWidgets.QMessageBox.critical(self, self.tr('Duplicate Fragment'), self.tr(
            'You just input fragment #%s, but that fragment has already been '
            'entered!' % fnum), QtWidgets.QMessageBox.Ok)
         return

         #if currType == '0':
         #   X = SecureBinaryData(''.join([fragData[i].toBinStr() for i in range(1, 5)]))
         #   Y = SecureBinaryData(''.join([fragData[i].toBinStr() for i in range(5, 9)]))
         #elif currType == BACKUP_TYPE_135A:
         #   X = SecureBinaryData(int_to_binary(fnum + 1, widthBytes=64, endOut=BIGENDIAN))
         #   Y = SecureBinaryData(''.join([fragData[i].toBinStr() for i in range(1, 5)]))
         #elif currType == BACKUP_TYPE_135C:
         #   X = SecureBinaryData(int_to_binary(fnum + 1, widthBytes=32, endOut=BIGENDIAN))
         #   Y = SecureBinaryData(''.join([fragData[i].toBinStr() for i in range(1, 3)]))

      self.fragDataMap[tableIndex] = [fragData[0][:], X.copy(), Y.copy()]

      X.destroy()
      Y.destroy()
      self.checkRestoreParams()

   #############################################################################
   def verifyNonDuplicateFrag(self, fnum):
      for row, data in self.fragDataMap.iteritems():
         rowFrag = ReadFragIDLineBin(data[0])[1]
         if fnum == rowFrag:
            return False

      return True

   #############################################################################
   def processFrags(self):
      if self.chkEncrypt.isChecked() and self.advancedOptionsTab.getKdfSec() == -1:
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid Target Compute Time'), \
            self.tr('You entered Target Compute Time incorrectly.\n\nEnter: <Number> (ms, s)'), QtWidgets.QMessageBox.Ok)
         return
      if self.chkEncrypt.isChecked() and self.advancedOptionsTab.getKdfBytes() == -1:
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid Max Memory Usage'), \
            self.tr('You entered Max Memory Usage incorrectly.\n\nEnter: <Number> (kB, MB)'), QtWidgets.QMessageBox.Ok)
         return
      SECPRINT = HardcodedKeyMaskParams()
      pwd, ekey = '', ''
      if self.displaySecureString.isVisible():
         pwd = str(self.displaySecureString.text()).strip()
         maskKey = SECPRINT['FUNC_KDF'](pwd)

      fragMtrx, M = [], -1
      for row, trip in self.fragDataMap.iteritems():
         M, fnum, wltID, doMask, fid = ReadFragIDLineBin(trip[0])
         X, Y = trip[1], trip[2]
         if doMask:
            LOGINFO('Row %d needs unmasking' % row)
            Y = SECPRINT['FUNC_UNMASK'](Y, ekey=maskKey)
         else:
            LOGINFO('Row %d is already unencrypted' % row)
         fragMtrx.append([X.toBinStr(), Y.toBinStr()])

      typeToBytes = {'0': 64, BACKUP_TYPE_135A: 64, BACKUP_TYPE_135C: 32}
      nBytes = typeToBytes[self.wltType]


      if self.thisIsATest and len(fragMtrx) > M:
         self.testFragSubsets(fragMtrx, M)
         return


      SECRET = ReconstructSecret(fragMtrx, M, nBytes)
      for i in range(len(fragMtrx)):
         fragMtrx[i] = []

      LOGINFO('Final length of frag mtrx: %d', len(fragMtrx))
      LOGINFO('Final length of secret:    %d', len(SECRET))

      priv, chain = '', ''
      #if len(SECRET) == 64:
         #priv = SecureBinaryData(SECRET[:32 ])
         #chain = SecureBinaryData(SECRET[ 32:])
      #elif len(SECRET) == 32:
         #priv = SecureBinaryData(SECRET)
         #chain = DeriveChaincodeFromRootKey(priv)


        # If we got here, the data is valid, let's create the wallet and accept the dlg
        # Now we should have a fully-plaintext rootkey and chaincode
      root = PyBtcAddress().createFromPlainKeyData(priv)
      root.chaincode = chain

      first = root.extendAddressChain()
      newWltID = binary_to_base58((ADDRBYTE + first.getAddr160()[:5])[::-1])

        # If this is a test, then bail
      if self.thisIsATest:
         verifyRecoveryTestID(self, newWltID, self.testWltID)
         return

      dlgOwnWlt = None
      if newWltID in self.main.walletMap:
         dlgOwnWlt = DlgReplaceWallet(newWltID, self.parent, self.main)

         if (dlgOwnWlt.exec_()):
            if dlgOwnWlt.output == 0:
               return
         else:
            self.reject()
            return

      reply = QtWidgets.QMessageBox.question(self, self.tr('Verify Wallet ID'), self.tr(
         'The data you entered corresponds to a wallet with the '
         'ID:<blockquote><b>{%s}</b></blockquote>Does this ID '
         'match the "Wallet Unique ID" printed on your paper backup? '
         'If not, click "No" and reenter key and chain-code data '
         'again.' % newWltID), QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)
      if reply == QtWidgets.QMessageBox.No:
         return


      passwd = []
      if self.chkEncrypt.isChecked():
         dlgPasswd = DlgChangePassphrase(self, self.main)
         #if dlgPasswd.exec_():
            #passwd = SecureBinaryData(str(dlgPasswd.edtPasswd1.text()))
         #else:
            #QtWidgets.QMessageBox.critical(self, self.tr('Cannot Encrypt'), self.tr(
               #'You requested your restored wallet be encrypted, but no '
               #'valid passphrase was supplied.  Aborting wallet '
               #'recovery.'), QtWidgets.QMessageBox.Ok)
               #return

      shortl = ''
      longl  = ''
      nPool  = 1000

      if dlgOwnWlt is not None:
         if dlgOwnWlt.Meta is not None:
            shortl = ' - %s' % (dlgOwnWlt.Meta['shortLabel'])
            longl  = dlgOwnWlt.Meta['longLabel']
            nPool = max(nPool, dlgOwnWlt.Meta['naddress'])

      if passwd:
         self.newWallet = PyBtcWallet().createNewWallet(\
                                  plainRootKey=priv, \
                                  chaincode=chain, \
                                  shortLabel='Restored - ' + newWltID + shortl, \
                                  longLabel=longl, \
                                  withEncrypt=True, \
                                  securePassphrase=passwd, \
                                  kdfTargSec=self.advancedOptionsTab.getKdfSec(), \
                                  kdfMaxMem=self.advancedOptionsTab.getKdfBytes(),
                                  isActuallyNew=False, \
                                  doRegisterWithBDM=False)
      else:
         self.newWallet = PyBtcWallet().createNewWallet(\
                                    plainRootKey=priv, \
                                    chaincode=chain, \
                                    shortLabel='Restored - ' + newWltID +shortl, \
                                    longLabel=longl, \
                                    withEncrypt=False, \
                                    isActuallyNew=False, \
                                    doRegisterWithBDM=False)


        # Will pop up a little "please wait..." window while filling addr pool
      fillAddrPoolProgress = DlgProgress(self, self.parent, HBar=1,
                                         Title=self.tr("Computing New Addresses"))
      fillAddrPoolProgress.exec_(self.newWallet.fillAddressPool, nPool)

      if dlgOwnWlt is not None:
         if dlgOwnWlt.Meta is not None:
            from armoryengine.PyBtcWallet import WLT_UPDATE_ADD
            for n_cmt in range(0, dlgOwnWlt.Meta['ncomments']):
               entrylist = []
               entrylist = list(dlgOwnWlt.Meta[n_cmt])
               self.newWallet.walletFileSafeUpdate([[WLT_UPDATE_ADD, entrylist[2], entrylist[1], entrylist[0]]])

         self.newWallet = PyBtcWallet().readWalletFile(self.newWallet.walletPath)
      self.accept()

    #############################################################################
   def testFragSubsets(self, fragMtrx, M):
        # If the user entered multiple fragments
      fragMap = {}
      for x, y in fragMtrx:
         fragMap[binary_to_int(x, BIGENDIAN) - 1] = [x, y]
      typeToBytes = {'0': 64, BACKUP_TYPE_135A: 64, BACKUP_TYPE_135C: 32}

      isRandom, results = testReconstructSecrets(fragMap, M, 100)
      def privAndChainFromRow(secret):
         priv, chain = None, None
         #if len(secret) == 64:
            #priv = SecureBinaryData(secret[:32 ])
            #chain = SecureBinaryData(secret[ 32:])
            #return (priv, chain)
         #elif len(secret) == 32:
            #priv = SecureBinaryData(secret)
            #chain = DeriveChaincodeFromRootKey(priv)
            #return (priv, chain)
         #else:
            #LOGERROR('Root secret is %s bytes ?!' % len(secret))
            #raise KeyDataError

      results = [(row[0], privAndChainFromRow(row[1])) for row in results]
      subsAndIDs = [(row[0], calcWalletIDFromRoot(*row[1])) for row in results]

      DlgShowTestResults(self, isRandom, subsAndIDs, \
         M, len(fragMtrx), self.testWltID).exec_()


################################################################################
class DlgEnterOneFrag(ArmoryDialog):

   def __init__(self, parent, main, fragList=[], wltType=UNKNOWN, securePrintCode=None):
      super(DlgEnterOneFrag, self).__init__(parent, main)
      self.fragData = []
      BLUE = htmlColor('TextBlue')
      already = ''
      if len(fragList) > 0:
         strList = ['<font color="%s">%d</font>' % (BLUE, f) for f in fragList]
         replStr = '[' + ','.join(strList[:]) + ']'
         already = self.tr('You have entered fragments %s, so far.' % replStr)

      lblDescr = QRichLabel(self.tr(
         '<b><u>Enter Another Fragment...</u></b> <br><br> %s '
         'The fragments can be entered in any order, as long as you provide '
         'enough of them to restore the wallet.  If any fragments use a '
         u'SecurePrint\u200b\u2122 code, please enter it once on the '
         'previous window, and it will be applied to all fragments that '
         'require it.' % already))

      self.version0Button = QtWidgets.QRadioButton(self.tr( BACKUP_TYPE_0_TEXT), self)
      self.version135aButton = QtWidgets.QRadioButton(self.tr( BACKUP_TYPE_135a_TEXT), self)
      self.version135aSPButton = QtWidgets.QRadioButton(self.tr( BACKUP_TYPE_135a_SP_TEXT), self)
      self.version135cButton = QtWidgets.QRadioButton(self.tr( BACKUP_TYPE_135c_TEXT), self)
      self.version135cSPButton = QtWidgets.QRadioButton(self.tr( BACKUP_TYPE_135c_SP_TEXT), self)
      self.backupTypeButtonGroup = QtWidgets.QButtonGroup(self)
      self.backupTypeButtonGroup.addButton(self.version0Button)
      self.backupTypeButtonGroup.addButton(self.version135aButton)
      self.backupTypeButtonGroup.addButton(self.version135aSPButton)
      self.backupTypeButtonGroup.addButton(self.version135cButton)
      self.backupTypeButtonGroup.addButton(self.version135cSPButton)
      self.version135cButton.setChecked(True)
      self.backupTypeButtonGroup.buttonClicked.connect(self.changeType)

      # This value will be locked after the first fragment is entered.
      if wltType == UNKNOWN:
         self.version135cButton.setChecked(True)
      elif wltType == '0':
         self.version0Button.setChecked(True)
         self.version135aButton.setEnabled(False)
         self.version135aSPButton.setEnabled(False)
         self.version135cButton.setEnabled(False)
         self.version135cSPButton.setEnabled(False)
      elif wltType == BACKUP_TYPE_135A:
            # Could be 1.35a with or without SecurePrintCode so remove the rest
         self.version0Button.setEnabled(False)
         self.version135cButton.setEnabled(False)
         self.version135cSPButton.setEnabled(False)
         if securePrintCode:
            self.version135aSPButton.setChecked(True)
         else:
            self.version135aButton.setChecked(True)
      elif wltType == BACKUP_TYPE_135C:
         # Could be 1.35c with or without SecurePrintCode so remove the rest
         self.version0Button.setEnabled(False)
         self.version135aButton.setEnabled(False)
         self.version135aSPButton.setEnabled(False)
         if securePrintCode:
            self.version135cSPButton.setChecked(True)
         else:
            self.version135cButton.setChecked(True)

      lblType = QRichLabel(self.tr('<b>Backup Type:</b>'), doWrap=False)

      layoutRadio = QtWidgets.QVBoxLayout()
      layoutRadio.addWidget(self.version0Button)
      layoutRadio.addWidget(self.version135aButton)
      layoutRadio.addWidget(self.version135aSPButton)
      layoutRadio.addWidget(self.version135cButton)
      layoutRadio.addWidget(self.version135cSPButton)
      layoutRadio.setSpacing(0)

      radioButtonFrame = QtWidgets.QFrame()
      radioButtonFrame.setLayout(layoutRadio)

      frmBackupType = makeVertFrame([lblType, radioButtonFrame])

      self.prfxList = ['x1:', 'x2:', 'x3:', 'x4:', \
                       'y1:', 'y2:', 'y3:', 'y4:', \
                       'F1:', 'F2:', 'F3:', 'F4:']
      self.prfxList = [QtWidgets.QLabel(p) for p in self.prfxList]
      inpMask = '<AAAA\ AAAA\ AAAA\ AAAA\ \ AAAA\ AAAA\ AAAA\ AAAA\ \ AAAA!'
      self.edtList = [MaskedInputLineEdit(inpMask) for i in range(12)]

      inpMaskID = '<HHHH\ HHHH\ HHHH\ HHHH!'
      self.lblID = QRichLabel('ID:')
      self.edtID = MaskedInputLineEdit(inpMaskID)

      frmAllInputs = QtWidgets.QFrame()
      frmAllInputs.setFrameStyle(STYLE_RAISED)
      layoutAllInp = QtWidgets.QGridLayout()

      # Add Secure Print row - Use supplied securePrintCode and
      # disable text entry if it is not None
      self.lblSP = QRichLabel(self.tr(u'SecurePrint\u200b\u2122 Code:'), doWrap=False)
      self.editSecurePrint = QtWidgets.QLineEdit()
      self.editSecurePrint.setEnabled(not securePrintCode)
      if (securePrintCode):
         self.editSecurePrint.setText(securePrintCode)
      self.frmSP = makeHorizFrame([STRETCH, self.lblSP, self.editSecurePrint])
      layoutAllInp.addWidget(self.frmSP, 0, 0, 1, 2)

      layoutAllInp.addWidget(self.lblID, 1, 0, 1, 1)
      layoutAllInp.addWidget(self.edtID, 1, 1, 1, 1)
      for i in range(12):
         layoutAllInp.addWidget(self.prfxList[i], i + 2, 0, 1, 2)
         layoutAllInp.addWidget(self.edtList[i], i + 2, 1, 1, 2)
      frmAllInputs.setLayout(layoutAllInp)

      self.btnAccept = QtWidgets.QPushButton(self.tr("Done"))
      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.btnAccept.clicked.connect(self.verifyUserInput)
      self.btnCancel.clicked.connect(self.reject)
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblDescr)
      layout.addWidget(HLINE())
      layout.addWidget(frmBackupType)
      layout.addWidget(frmAllInputs)
      layout.addWidget(buttonBox)
      self.setLayout(layout)


      self.setWindowTitle(self.tr('Restore Single-Sheet Backup'))
      self.setMinimumWidth(500)
      self.layout().setSizeConstraint(QtWidgets.QLayout.SetFixedSize)
      self.changeType(self.backupTypeButtonGroup.checkedId())


   #############################################################################
   def changeType(self, sel):
      #            |-- X --| |-- Y --| |-- F --|
      if sel == self.version0Button:
         visList = [1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0]
      elif sel == self.version135aButton or \
           sel == self.version135aSPButton:
         visList = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1]
      elif sel == self.version135cButton or \
           sel == self.version135cSPButton:
         visList = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0]
      else:
         LOGERROR('What the heck backup type is selected?  %d', sel)
         return

      self.frmSP.setVisible(sel == self.version135aSPButton or \
                            sel == self.version135cSPButton)
      for i in range(12):
         self.prfxList[i].setVisible(visList[i] == 1)
         self.edtList[ i].setVisible(visList[i] == 1)



   #############################################################################
   def destroyFragData(self):
      for line in self.fragData:
         if not isinstance(line, basestring):
            # It's an SBD Object.  Destroy it.
            line.destroy()

   #############################################################################
   def isSecurePrintID(self):
      return hex_to_int(str(self.edtID.text()[:2])) > 127

   #############################################################################
   def verifyUserInput(self):
      self.fragData = []
      nError = 0
      rawBin = None

      sel = self.backupTypeButtonGroup.checkedId()
      rng = [-1]
      if   sel == self.version0Button:
         rng = range(8)
      elif sel == self.version135aButton or \
           sel == self.version135aSPButton:
         rng = range(8, 12)
      elif sel == self.version135cButton or \
           sel == self.version135cSPButton:
         rng = range(8, 10)


      if sel == self.version135aSPButton or \
         sel == self.version135cSPButton:
         # Prepare the key mask parameters
         SECPRINT = HardcodedKeyMaskParams()
         securePrintCode = str(self.editSecurePrint.text()).strip()
         if not checkSecurePrintCode(self, SECPRINT, securePrintCode):
            return
      elif self.isSecurePrintID():
            QtWidgets.QMessageBox.critical(self, 'Bad Encryption Code', self.tr(
               'The ID field indicates that this is a SecurePrint™ '
               'Backup Type. You have either entered the ID incorrectly or '
               'have chosen an incorrect Backup Type.'), QtWidgets.QMessageBox.Ok)
            return
      for i in rng:
         hasError = False
         try:
            rawEntry = str(self.edtList[i].text())
            rawBin, err = readSixteenEasyBytes(rawEntry.replace(' ', ''))
            if err == 'Error_2+':
               hasError = True
            elif err == 'Fixed_1':
               nError += 1
         except KeyError:
            hasError = True

         if hasError:
            reply = QtWidgets.QMessageBox.critical(self, self.tr('Verify Wallet ID'), self.tr(
               'There is an error in the data you entered that could not be '
               'fixed automatically.  Please double-check that you entered the '
               'text exactly as it appears on the wallet-backup page. <br><br> '
               'The error occured on the "%s" line.' % str(self.prfxList[i].text())), QtWidgets.QMessageBox.Ok)
            LOGERROR('Error in wallet restore field')
            self.prfxList[i].setText('<font color="red">' + str(self.prfxList[i].text()) + '</font>')
            self.destroyFragData()
            return

         self.fragData.append(SecureBinaryData(rawBin))
         rawBin = None


      idLine = str(self.edtID.text()).replace(' ', '')
      self.fragData.insert(0, hex_to_binary(idLine))

      M, fnum, wltID, doMask, fid = ReadFragIDLineBin(self.fragData[0])

      reply = QtWidgets.QMessageBox.question(self, self.tr('Verify Fragment ID'), self.tr(
         'The data you entered is for fragment: '
         '<br><br> <font color="%s" size=3><b>%s</b></font>  <br><br> '
         'Does this ID match the "Fragment:" field displayed on your backup? '
         'If not, click "No" and re-enter the fragment data.' % (htmlColor('TextBlue'), fid)), QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)

      if reply == QtWidgets.QMessageBox.Yes:
         self.accept()


################################################################################
class DlgRestoreWOData(ArmoryDialog):
   #############################################################################
   def __init__(self, parent, main, thisIsATest=False, expectWltID=None):
      super(DlgRestoreWOData, self).__init__(parent, main)

      self.thisIsATest = thisIsATest
      self.testWltID = expectWltID
      headerStr = ''
      lblDescr = ''

      # Write the text at the top of the window.
      if thisIsATest:
         lblDescr = QRichLabel(self.tr(
          '<b><u><font color="blue" size="4">Test a Watch-Only Wallet Restore '
          '</font></u></b><br><br>'
          'Use this window to test the restoration of a watch-only wallet using '
          'the wallet\'s data. You can either type the data on a root data '
          'printout or import the data from a file.'))
      else:
         lblDescr = QRichLabel(self.tr(
            '<b><u><font color="blue" size="4">Restore a Watch-Only Wallet '
            '</font></u></b><br><br>'
            'Use this window to restore a watch-only wallet using the wallet\'s '
            'data. You can either type the data on a root data printout or import '
            'the data from a file.'))

      # Create the line that will contain the imported ID.
      self.rootIDLabel = QRichLabel(self.tr('Watch-Only Root ID:'), doWrap=False)
      inpMask = '<AAAA\ AAAA\ AAAA\ AAAA\ AA!'
      self.rootIDLine = MaskedInputLineEdit(inpMask)
      self.rootIDLine.setFont(GETFONT('Fixed', 9))
      self.rootIDFrame = makeHorizFrame([STRETCH, self.rootIDLabel, \
                                           self.rootIDLine])

      # Create the lines that will contain the imported key/code data.
      self.pkccLList = [QtWidgets.QLabel(self.tr('Data:')), QtWidgets.QLabel(''), QtWidgets.QLabel(''), QtWidgets.QLabel('')]
      for y in self.pkccLList:
         y.setFont(GETFONT('Fixed', 9))
      inpMask = '<AAAA\ AAAA\ AAAA\ AAAA\ \ AAAA\ AAAA\ AAAA\ AAAA\ \ AAAA!'
      self.pkccList = [MaskedInputLineEdit(inpMask) for i in range(4)]
      for x in self.pkccList:
         x.setFont(GETFONT('Fixed', 9))

      # Build the frame that will contain both the ID and the key/code data.
      frmAllInputs = QtWidgets.QFrame()
      frmAllInputs.setFrameStyle(STYLE_RAISED)
      layoutAllInp = QtWidgets.QGridLayout()
      layoutAllInp.addWidget(self.rootIDFrame, 0, 0, 1, 2)
      for i in range(4):
         layoutAllInp.addWidget(self.pkccLList[i], i + 1, 0)
         layoutAllInp.addWidget(self.pkccList[i], i + 1, 1)
      frmAllInputs.setLayout(layoutAllInp)

      # Put together the button code.
      doItText = self.tr('Test Backup') if thisIsATest else self.tr('Restore Wallet')
      self.btnLoad   = QtWidgets.QPushButton(self.tr("Load From Text File"))
      self.btnAccept = QtWidgets.QPushButton(doItText)
      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.btnLoad.clicked.connect(self.loadWODataFile)
      self.btnAccept.clicked.connect(self.verifyUserInput)
      self.btnCancel.clicked.connect(self.reject)
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnLoad, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)

      # Set the final window layout.
      finalLayout = QtWidgets.QVBoxLayout()
      finalLayout.addWidget(lblDescr)
      finalLayout.addWidget(makeHorizFrame(['Stretch',self.btnLoad]))
      finalLayout.addWidget(HLINE())
      finalLayout.addWidget(frmAllInputs)
      finalLayout.addWidget(makeHorizFrame([self.btnCancel, 'Stretch', self.btnAccept]))
      finalLayout.setStretch(0, 0)
      finalLayout.setStretch(1, 0)
      finalLayout.setStretch(2, 0)
      finalLayout.setStretch(3, 0)
      finalLayout.setStretch(4, 0)
      finalLayout.setStretch(4, 1)
      finalLayout.setStretch(4, 2)
      self.setLayout(finalLayout)

      # Set window title.
      if thisIsATest:
         self.setWindowTitle(self.tr('Test Watch-Only Wallet Backup'))
      else:
         self.setWindowTitle(self.tr('Restore Watch-Only Wallet Backup'))

      # Set final window layout options.
      self.setMinimumWidth(550)
      self.layout().setSizeConstraint(QtWidgets.QLayout.SetFixedSize)


   #############################################################################
   def loadWODataFile(self):
      '''Function for loading a root public key/chain code (\"pkcc\") file.'''
      fn = self.main.getFileLoad(self.tr('Import Wallet File'),
                                 ffilter=[self.tr('Root Pubkey Text Files (*.rootpubkey)')])
      if not os.path.exists(fn):
         return

      # Read in the data.
      # Protip: readlines() leaves in '\n'. read().splitlines() nukes '\n'.
      loadFile = open(fn, 'rb')
      fileLines = loadFile.read().splitlines()
      loadFile.close()

      # Confirm that we have an actual PKCC file.
      pkccFileVer = int(fileLines[0], 10)
      if pkccFileVer != 1:
         return
      else:
         self.rootIDLine.setText(str(fileLines[1]))
         for curLineNum, curLine in enumerate(fileLines[2:6]):
            self.pkccList[curLineNum].setText(str(curLine))


   #############################################################################
   def verifyUserInput(self):
      '''Function that verifies the input for a root public key/chain code
         restoration validation.'''
      inRootChecked = ''
      inputLines = []
      nError = 0
      rawBin = None
      nLine = 4
      hasError = False

      # Read in the root ID data and handle any errors.
      try:
         rawID = easyType16_to_binary(str(self.rootIDLine.text()).replace(' ', ''))
         if len(rawID) != 9:
            raise ValueError('Must supply 9 byte input for the ID')

         # Grab the data and apply the checksum to make sure it's okay.
         inRootData = rawID[:7]   # 7 bytes
         inRootChksum = rawID[7:] # 2 bytes
         inRootChecked = verifyChecksum(inRootData, inRootChksum)
         if len(inRootChecked) != 7:
            hasError = True
         elif inRootChecked != inRootData:
            nError += 1
      except:
         hasError = True

      # If the root ID is busted, stop.
      if hasError:
         (errType, errVal) = sys.exc_info()[:2]
         reply = QtWidgets.QMessageBox.critical(self, self.tr('Invalid Data'), self.tr(
               'There is an error in the root ID you entered that could not '
               'be fixed automatically.  Please double-check that you entered the '
               'text exactly as it appears on the wallet-backup page.<br><br>'),
               QtWidgets.QMessageBox.Ok)
         LOGERROR('Error in root ID restore field')
         LOGERROR('Error Type: %s', errType)
         LOGERROR('Error Value: %s', errVal)
         return

      # Save the version/key byte and the root ID. For now, ignore the version.
      inRootVer = inRootChecked[0]  # 1 byte
      inRootID = inRootChecked[1:7] # 6 bytes

      # Read in the root data (public key & chain code) and handle any errors.
      for i in range(nLine):
         hasError = False
         try:
            rawEntry = str(self.pkccList[i].text())
            rawBin, err = readSixteenEasyBytes(rawEntry.replace(' ', ''))
            if err == 'Error_2+':  # 2+ bytes are wrong, so we need to stop.
               hasError = True
            elif err == 'Fixed_1': # 1 byte is wrong, so we may be okay.
               nError += 1
         except:
            hasError = True

         # If the root ID is busted, stop.
         if hasError:
            lineNumber = i+1
            reply = QtWidgets.QMessageBox.critical(self, self.tr('Invalid Data'), self.tr(
               'There is an error in the root data you entered that could not be '
               'fixed automatically.  Please double-check that you entered the '
               'text exactly as it appears on the wallet-backup page.  <br><br>'
               'The error occured on <font color="red">line #%d</font>.' % lineNumber), QtWidgets.QMessageBox.Ok)
            LOGERROR('Error in root data restore field')
            return

         # If we've gotten this far, save the incoming line.
         inputLines.append(rawBin)

      # Set up the root ID data.
      pkVer = binary_to_int(inRootVer) & PYROOTPKCCVERMASK  # Ignored for now.
      pkSignByte = ((binary_to_int(inRootVer) & PYROOTPKCCSIGNMASK) >> 7) + 2
      rootPKComBin = int_to_binary(pkSignByte) + ''.join(inputLines[:2])
      rootPubKey = CryptoECDSA().UncompressPoint(SecureBinaryData(rootPKComBin))
      rootChainCode = SecureBinaryData(''.join(inputLines[2:]))

      # Now we should have a fully-plaintext root key and chain code, and can
      # get some related data.
      root = PyBtcAddress().createFromPublicKeyData(rootPubKey)
      root.chaincode = rootChainCode
      first = root.extendAddressChain()
      newWltID = binary_to_base58(inRootID)

      # Stop here if this was just a test
      if self.thisIsATest:
         verifyRecoveryTestID(self, newWltID, self.testWltID)
         return

      # If we already have the wallet, don't replace it, otherwise proceed.
      dlgOwnWlt = None
      if newWltID in self.main.walletMap:
         QtWidgets.QMessageBox.warning(self, self.tr('Wallet Already Exists'), self.tr(
                             'The wallet already exists and will not be '
                             'replaced.'), QtWidgets.QMessageBox.Ok)
         self.reject()
         return
      else:
         # Make sure the user is restoring the wallet they want to restore.
         reply = QtWidgets.QMessageBox.question(self, self.tr('Verify Wallet ID'), \
                  self.tr('The data you entered corresponds to a wallet with a wallet '
                  'ID: \n\n\t%s\n\nDoes this '
                  'ID match the "Wallet Unique ID" you intend to restore? '
                  'If not, click "No" and enter the key and chain-code data '
                  'again.' % binary_to_base58(inRootID)), QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)
         if reply == QtWidgets.QMessageBox.No:
            return

         # Create the wallet.
         self.newWallet = PyBtcWallet().createNewWalletFromPKCC(rootPubKey, \
                                                                   rootChainCode)

         # Create some more addresses and show a progress bar while restoring.
         nPool = 1000
         fillAddrPoolProgress = DlgProgress(self, self.main, HBar=1,
                                            Title=self.tr("Computing New Addresses"))
         fillAddrPoolProgress.exec_(self.newWallet.fillAddressPool, nPool)

      self.accept()


################################################################################
class DlgEnterSecurePrintCode(ArmoryDialog):

   def __init__(self, parent, main):
      super(DlgEnterSecurePrintCode, self).__init__(parent, main)

      lblSecurePrintCodeDescr = QRichLabel(self.tr(
         u'This fragment file requires a SecurePrint\u200b\u2122 code. '
         'You will only have to enter this code once since it is the same '
         'on all fragments.'))
      lblSecurePrintCodeDescr.setMinimumWidth(440)
      self.lblSP = QRichLabel(self.tr(u'SecurePrint\u200b\u2122 Code: '), doWrap=False)
      self.editSecurePrint = QtWidgets.QLineEdit()
      spFrame = makeHorizFrame([self.lblSP, self.editSecurePrint, STRETCH])

      self.btnAccept = QtWidgets.QPushButton(self.tr("Done"))
      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.btnAccept.clicked.connect(self.verifySecurePrintCode)
      self.btnCancel.clicked.connect(self.reject)
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblSecurePrintCodeDescr)
      layout.addWidget(spFrame)
      layout.addWidget(buttonBox)
      self.setLayout(layout)
      self.setWindowTitle(self.tr('Enter Secure Print Code'))

   def verifySecurePrintCode(self):
      # Prepare the key mask parameters
      SECPRINT = HardcodedKeyMaskParams()
      securePrintCode = str(self.editSecurePrint.text()).strip()

      if not checkSecurePrintCode(self, SECPRINT, securePrintCode):
         return

      self.accept()


################################################################################
def OpenPaperBackupDialog(backupType, parent, main, wlt, unlockTitle=None):
   result = True
   verifyText = ''
   if backupType == 'Single':
      from qtdialogs.DlgBackupCenter import DlgPrintBackup
      result = DlgPrintBackup(parent, main, wlt).exec_()
      verifyText = parent.tr(
         u'If the backup was printed with SecurePrint\u200b\u2122, please '
         u'make sure you wrote the SecurePrint\u200b\u2122 code on the '
         'printed sheet of paper. Note that the code <b><u>is</u></b> '
         'case-sensitive!')
   elif backupType == 'Frag':
      result = DlgFragBackup(parent, main, wlt).exec_()
      verifyText = parent.tr(
         u'If the backup was created with SecurePrint\u200b\u2122, please '
         u'make sure you wrote the SecurePrint\u200b\u2122 code on each '
         'fragment (or stored with each file fragment). The code is the '
         'same for all fragments.')

   doTest = MsgBoxCustom(MSGBOX.Warning, parent.tr('Verify Your Backup!'), parent.tr(
      '<b><u>Verify your backup!</u></b> '
      '<br><br>'
      'If you just made a backup, make sure that it is correct! '
      'The following steps are recommended to verify its integrity: '
      '<br>'
      '<ul>'
      '<li>Verify each line of the backup data contains <b>9 columns</b> '
      'of <b>4 letters each</b> (excluding any "ID" lines).</li> '
      '<li>%s</li>'
      '<li>Use Armory\'s backup tester to test the backup before you '
      'physiclly secure it.</li> '
      '</ul>'
      '<br>'
      'Armory has a backup tester that uses the exact same '
      'process as restoring your wallet, but stops before it writes any '
      'data to disk.  Would you like to test your backup now? '
       % verifyText), yesStr="Test Backup", noStr="Cancel")

   if doTest:
      if backupType == 'Single':
         DlgRestoreSingle(parent, main, True, wlt.uniqueIDB58).exec_()
      elif backupType == 'Frag':
         DlgRestoreFragged(parent, main, True, wlt.uniqueIDB58).exec_()

   return result


################################################################################
def verifyRecoveryTestID(parent, computedWltID, expectedWltID=None):

   if expectedWltID == None:
      # Testing an arbitrary paper backup
      yesno = QtWidgets.QMessageBox.question(parent, parent.tr('Recovery Test'), parent.tr(
         'From the data you entered, Armory calculated the following '
         'wallet ID: <font color="blue"><b>%s</b></font> '
         '<br><br>'
         'Does this match the wallet ID on the backup you are '
         'testing?' % computedWltID), QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)

      if yesno == QtWidgets.QMessageBox.No:
         QtWidgets.QMessageBox.critical(parent, parent.tr('Bad Backup!'), parent.tr(
            'If this is your only backup and you are sure that you entered '
            'the data correctly, then it is <b>highly recommended you stop using '
            'this wallet!</b>  If this wallet currently holds any funds, '
            'you should move the funds to a wallet that <u>does</u> '
            'have a working backup. '
            '<br><br> <br><br>'
            'Wallet ID of the data you entered: %s <br>' % computedWltID), \
            QtWidgets.QMessageBox.Ok)
      elif yesno == QtWidgets.QMessageBox.Yes:
         MsgBoxCustom(MSGBOX.Good, parent.tr('Backup is Good!'), parent.tr(
            '<b>Your backup works!</b> '
            '<br><br>'
            'The wallet ID is computed from a combination of the root '
            'private key, the "chaincode" and the first address derived '
            'from those two pieces of data.  A matching wallet ID '
            'guarantees it will produce the same chain of addresses as '
            'the original.'))
   else:  # an expected wallet ID was supplied
      if not computedWltID == expectedWltID:
         QtWidgets.QMessageBox.critical(parent, parent.tr('Bad Backup!'), parent.tr(
            'If you are sure that you entered the backup information '
            'correctly, then it is <b>highly recommended you stop using '
            'this wallet!</b>  If this wallet currently holds any funds, '
            'you should move the funds to a wallet that <u>does</u> '
            'have a working backup.'
            '<br><br>'
            'Computed wallet ID: %s <br>'
            'Expected wallet ID: %s <br><br>'
            'Is it possible that you loaded a different backup than the '
            'one you just made?' % (computedWltID, expectedWltID)), \
            QtWidgets.QMessageBox.Ok)
      else:
         MsgBoxCustom(MSGBOX.Good, parent.tr('Backup is Good!'), parent.tr(
            'Your backup works! '
            '<br><br> '
            'The wallet ID computed from the data you entered matches '
            'the expected ID.  This confirms that the backup produces '
            'the same sequence of private keys as the original wallet! '
            '<br><br> '
            'Computed wallet ID: %s <br> '
            'Expected wallet ID: %s <br> '
            '<br>' % (computedWltID, expectedWltID )))
