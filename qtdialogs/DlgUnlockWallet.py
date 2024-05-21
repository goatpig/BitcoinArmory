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

from ui.QtExecuteSignal import TheSignalExecution
from armoryengine.CppBridge import ServerPush

from qtdialogs.ArmoryDialog import ArmoryDialog
from armoryengine.Settings import TheSettings
from qtdialogs.qtdefines import makeHorizFrame, STRETCH, \
   MIN_PASSWD_WIDTH, LetterButton, createToolTipWidget
from qtpy import QtWidgets

################################################################################
class DlgUnlockWallet(ArmoryDialog):
   def __init__(self, wltID, parent=None, main=None, unlockMsg='Unlock'):
      super(DlgUnlockWallet, self).__init__(parent, main)
      self.wltID = wltID

      ##### Upper layout
      lblDescr = QtWidgets.QLabel(
         self.tr("Enter your passphrase to unlock this wallet"))
      lblPasswd = QtWidgets.QLabel(self.tr("Passphrase:"))
      self.edtPasswd = QtWidgets.QLineEdit()
      self.edtPasswd.setEchoMode(QtWidgets.QLineEdit.Password)
      self.edtPasswd.setMinimumWidth(MIN_PASSWD_WIDTH(self))
      self.edtPasswd.setSizePolicy(
         QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Expanding)

      self.btnAccept = QtWidgets.QPushButton(self.tr("Unlock"))
      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.btnAccept.clicked.connect(self.acceptPassphrase)
      self.btnCancel.clicked.connect(self.rejectPassphrase)
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)

      layoutUpper = QtWidgets.QGridLayout()
      layoutUpper.addWidget(lblDescr, 1, 0, 1, 2)
      layoutUpper.addWidget(lblPasswd, 2, 0, 1, 1)
      layoutUpper.addWidget(self.edtPasswd, 2, 1, 1, 1)
      self.frmUpper = QtWidgets.QFrame()
      self.frmUpper.setLayout(layoutUpper)

      ##### Lower layout
      # Add scrambled keyboard (EN-US only)

      ttipScramble = createToolTipWidget(\
         self.tr('Using a visual keyboard to enter your passphrase '
         'protects you against simple keyloggers.   Scrambling '
         'makes it difficult to use, but prevents even loggers '
         'that record mouse clicks.'))

      self.createKeyButtons()
      self.rdoScrambleNone = QtWidgets.QRadioButton(self.tr('Regular Keyboard'))
      self.rdoScrambleLite = QtWidgets.QRadioButton(self.tr('Scrambled (Simple)'))
      self.rdoScrambleFull = QtWidgets.QRadioButton(self.tr('Scrambled (Dynamic)'))
      btngrp = QtWidgets.QButtonGroup(self)
      btngrp.addButton(self.rdoScrambleNone)
      btngrp.addButton(self.rdoScrambleLite)
      btngrp.addButton(self.rdoScrambleFull)
      btngrp.setExclusive(True)
      defaultScramble = TheSettings.getSettingOrSetDefault('ScrambleDefault', 0)
      if defaultScramble == 0:
         self.rdoScrambleNone.setChecked(True)
      elif defaultScramble == 1:
         self.rdoScrambleLite.setChecked(True)
      elif defaultScramble == 2:
         self.rdoScrambleFull.setChecked(True)
      self.rdoScrambleNone.clicked.connect(self.changeScramble)
      self.rdoScrambleLite.clicked.connect(self.changeScramble)
      self.rdoScrambleFull.clicked.connect(self.changeScramble)
      btnRowFrm = makeHorizFrame([
         self.rdoScrambleNone,
         self.rdoScrambleLite,
         self.rdoScrambleFull,
         STRETCH
      ])

      self.layoutKeyboard = QtWidgets.QGridLayout()
      self.frmKeyboard = QtWidgets.QFrame()
      self.frmKeyboard.setLayout(self.layoutKeyboard)

      showOSD = TheSettings.getSettingOrSetDefault('KeybdOSD', False)
      self.layoutLower = QtWidgets.QGridLayout()
      self.layoutLower.addWidget(btnRowFrm , 0, 0)
      self.layoutLower.addWidget(self.frmKeyboard , 1, 0)
      self.frmLower = QtWidgets.QFrame()
      self.frmLower.setLayout(self.layoutLower)
      self.frmLower.setVisible(showOSD)

      ##### Expand button
      self.btnShowOSD = QtWidgets.QPushButton(self.tr('Show Keyboard >>>'))
      self.btnShowOSD.setCheckable(True)
      self.btnShowOSD.setChecked(showOSD)
      if showOSD:
         self.toggleOSD()
      self.btnShowOSD.toggled.connect(self.toggleOSD)
      frmAccept = makeHorizFrame([self.btnShowOSD, ttipScramble, STRETCH, buttonBox])

      ##### Complete Layout
      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(self.frmUpper)
      layout.addWidget(frmAccept)
      layout.addWidget(self.frmLower)
      self.setLayout(layout)
      self.setWindowTitle(unlockMsg + ' - ' + self.wltID)

      # Add scrambled keyboard
      self.layout().setSizeConstraint(QtWidgets.QLayout.SetFixedSize)
      self.changeScramble()
      self.redrawKeys()
      self.encryptionKeyIds = []

   #############################################################################
   def toggleOSD(self, *args):
      isChk = self.btnShowOSD.isChecked()
      TheSettings.set('KeybdOSD', isChk)
      self.frmLower.setVisible(isChk)
      if isChk:
         self.btnShowOSD.setText(self.tr('Hide Keyboard <<<'))
      else:
         self.btnShowOSD.setText(self.tr('Show Keyboard >>>'))

   #############################################################################
   def createKeyboardKeyButton(self, keyLow, keyUp, defRow, special=None):
      theBtn = LetterButton(keyLow, keyUp, defRow, special, self.edtPasswd, self)
      theBtn.clicked.connect(theBtn.insertLetter)
      theBtn.setMaximumWidth(40)
      return theBtn

   #############################################################################
   def redrawKeys(self):
      for btn in self.btnList:
         btn.setText(btn.upper if self.btnShift.isChecked() else btn.lower)
      self.btnShift.setText(self.tr('SHIFT'))
      self.btnSpace.setText(self.tr('SPACE'))
      self.btnDelete.setText(self.tr('DEL'))

   #############################################################################
   def deleteKeyboard(self):
      for btn in self.btnList:
         btn.setParent(None)
         del btn
      self.btnList = []
      self.btnShift.setParent(None)
      self.btnSpace.setParent(None)
      self.btnDelete.setParent(None)
      del self.btnShift
      del self.btnSpace
      del self.btnDelete
      del self.frmKeyboard
      del self.layoutKeyboard

   #############################################################################
   def createKeyButtons(self):
      # TODO:  Add some locale-agnostic method here, that could replace
      #        the letter arrays with something more appropriate for non en-us
      self.letLower = r"`1234567890-=qwertyuiop[]\asdfghjkl;'zxcvbnm,./"
      self.letUpper = r'~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:"ZXCVBNM<>?'
      self.letRows = r'11111111111112222222222222333333333334444444444'
      self.letPairs = zip(self.letLower, self.letUpper, self.letRows)

      self.btnList = []
      for l, u, r in zip(self.letLower, self.letUpper, self.letRows):
         if l == '7':
            # Because QPushButtons interpret ampersands as special characters
            u = 2 * u

         if l.isdigit():
            self.btnList.append(self.createKeyboardKeyButton('#' + l, u, int(r)))
         else:
            self.btnList.append(self.createKeyboardKeyButton(l, u, int(r)))

      # Add shift and space keys
      self.btnShift = self.createKeyboardKeyButton('', '', 5, 'shift')
      self.btnSpace = self.createKeyboardKeyButton(' ', ' ', 5, 'space')
      self.btnDelete = self.createKeyboardKeyButton(' ', ' ', 5, 'delete')
      self.btnShift.setCheckable(True)
      self.btnShift.setChecked(False)

   #############################################################################
   def reshuffleKeys(self):
      if self.rdoScrambleFull.isChecked():
         self.changeScramble()

   #############################################################################
   def changeScramble(self):
      self.deleteKeyboard()
      self.frmKeyboard = QtWidgets.QFrame()
      self.layoutKeyboard = QtWidgets.QGridLayout()
      self.createKeyButtons()

      if self.rdoScrambleNone.isChecked():
         opt = 0
         prevRow = 1
         col = 0
         for btn in self.btnList:
            row = btn.defRow
            if not row == prevRow:
               col = 0
            if row > 3 and col == 0:
               col += 1
            prevRow = row
            self.layoutKeyboard.addWidget(btn, row, col)
            col += 1
         self.layoutKeyboard.addWidget(self.btnShift, self.btnShift.defRow, 0, 1, 3)
         self.layoutKeyboard.addWidget(self.btnSpace, self.btnSpace.defRow, 4, 1, 5)
         self.layoutKeyboard.addWidget(self.btnDelete, self.btnDelete.defRow, 11, 1, 2)
         self.btnShift.setMaximumWidth(1000)
         self.btnSpace.setMaximumWidth(1000)
         self.btnDelete.setMaximumWidth(1000)
      elif self.rdoScrambleLite.isChecked():
         opt = 1
         nchar = len(self.btnList)
         rnd = SecureBinaryData().GenerateRandom(2 * nchar).toBinStr()
         newBtnList = [[self.btnList[i], rnd[2 * i:2 * (i + 1)]] for i in range(nchar)]
         newBtnList.sort(key=lambda x: x[1])
         prevRow = 0
         col = 0
         for i, btn in enumerate(newBtnList):
            row = i / 12
            if not row == prevRow:
               col = 0
            prevRow = row
            self.layoutKeyboard.addWidget(btn[0], row, col)
            col += 1
         self.layoutKeyboard.addWidget(self.btnShift, self.btnShift.defRow, 0, 1, 3)
         self.layoutKeyboard.addWidget(self.btnSpace, self.btnSpace.defRow, 4, 1, 5)
         self.layoutKeyboard.addWidget(self.btnDelete, self.btnDelete.defRow, 10, 1, 2)
         self.btnShift.setMaximumWidth(1000)
         self.btnSpace.setMaximumWidth(1000)
         self.btnDelete.setMaximumWidth(1000)
      elif self.rdoScrambleFull.isChecked():
         opt = 2
         extBtnList = self.btnList[:]
         extBtnList.extend([self.btnShift, self.btnSpace])
         nchar = len(extBtnList)
         rnd = SecureBinaryData().GenerateRandom(2 * nchar).toBinStr()
         newBtnList = [[extBtnList[i], rnd[2 * i:2 * (i + 1)]] for i in range(nchar)]
         newBtnList.sort(key=lambda x: x[1])
         prevRow = 0
         col = 0
         for i, btn in enumerate(newBtnList):
            row = i / 12
            if not row == prevRow:
               col = 0
            prevRow = row
            self.layoutKeyboard.addWidget(btn[0], row, col)
            col += 1
         self.layoutKeyboard.addWidget(self.btnDelete, self.btnDelete.defRow - 1, 11, 1, 2)
         self.btnShift.setMaximumWidth(40)
         self.btnSpace.setMaximumWidth(40)
         self.btnDelete.setMaximumWidth(40)

      self.frmKeyboard.setLayout(self.layoutKeyboard)
      self.layoutLower.addWidget(self.frmKeyboard, 1, 0)
      TheSettings.set('ScrambleDefault', opt)
      self.redrawKeys()

   #############################################################################
   def recycle(self):
      QtWidgets.QMessageBox.critical(self, self.tr('Invalid Passphrase'), \
         self.tr('That passphrase is not correct!'), QtWidgets.QMessageBox.Ok)
      self.edtPasswd.setText('')

   #############################################################################
   def completed(self):
      self.edtPasswd.setText('')
      self.accept()

   #############################################################################
   def acceptPassphrase(self):
      passphraseStr = str(self.edtPasswd.text())

      self.reply(passphraseStr)
      passphraseStr = ''

   #############################################################################
   def rejectPassphrase(self):
      self.edtPasswd.setText('')
      self.reply("")
      self.reject()

   #############################################################################
   def accept(self):
      self.edtPasswd.setText('')
      super().accept()

   #############################################################################
   def reject(self):
      self.edtPasswd.setText('')
      super().reject()

   #############################################################################
   def reply(self, passphrase):
      raise Exception("override me")

   #############################################################################
   def setIds(self, ids):
      if len(ids) == 0:
         self.reject()
      elif len(self.encryptionKeyIds) == 0:
         self.encryptionKeyIds = ids
         self.exec_()
      elif self.encryptionKeyIds != ids:
         raise Exception("encryption key ids mismtach")
      else:
         self.recycle()
         self.show()

################################################################################
class UnlockWalletHandler(ServerPush, DlgUnlockWallet):
   def __init__(self, wltId, title, parent):
      ServerPush.__init__(self)
      DlgUnlockWallet.__init__(self, main=parent, parent=parent,
         wltID=wltId, unlockMsg=title)

   #############################################################################
   def parseProtoPacket(self, protoPacket):
      def processPacket(theDialog, protoPacket):
         if protoPacket.HasField('cleanup'):
            theDialog.reject()
            return
         elif protoPacket.HasField('unlock_request'):
            theDialog.setIds(protoPacket.unlock_request.encryption_key_ids)
      TheSignalExecution.executeMethod(processPacket, self, protoPacket)

   #############################################################################
   def reply(self, passphrase):
      packet = self.getNewPacket()
      packet.success = bool(len(passphrase) != 0)
      packet.passphrase = passphrase
      super().reply()
