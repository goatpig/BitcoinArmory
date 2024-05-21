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

from qtpy import QtGui, QtWidgets

from armoryengine.ArmoryUtils import ADDRBYTE, LOGWARN, P2SHBYTE, isASCII
from armoryengine.AddressUtils import addrStr_to_hash160
from jasvet import ASv0, ASv1B64, ASv1CS, readSigBlock, verifySignature
from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.DlgUnlockWallet import DlgUnlockWallet
from qtdialogs.qtdefines import MSGBOX, QRichLabel, makeHorizFrame
from qtdialogs.MsgBoxCustom import MsgBoxCustom
from qtdialogs.DlgAddressBook import createAddrBookButton

class MessageSigningVerificationDialog(ArmoryDialog):

   def __init__(self, parent=None, main=None):
      super(MessageSigningVerificationDialog, self).__init__(parent, main)
      layout = QtWidgets.QVBoxLayout()
      self.setWindowTitle(self.tr("Message Signing/Verification"))
      self.setWindowIcon(QtGui.QIcon( self.main.iconfile))
      self.setMinimumWidth(600)
      
      tabbedPanel = QtWidgets.QTabWidget()
      messageSigningTab = MessageSigningWidget(parent, main)
      bareSignatureVerificationTab = BareSignatureVerificationWidget(parent, main)
      signedMsgBlockVerificationTab = SignedMessageBlockVerificationWidget(parent, main)
      tabbedPanel.addTab(messageSigningTab, self.tr("Sign Message"))
      tabbedPanel.addTab(bareSignatureVerificationTab, self.tr("Verify Bare Signature"))
      tabbedPanel.addTab(signedMsgBlockVerificationTab, self.tr("Verify Signed Message Block"))
      layout.addWidget(tabbedPanel)
      
      self.goBackButton = QtWidgets.QPushButton(self.tr("Done"))
      actionButtonBox = QtWidgets.QDialogButtonBox()
      actionButtonBox.addButton(self.goBackButton, QtWidgets.QDialogButtonBox.RejectRole)
      layout.addWidget(actionButtonBox)

      self.setLayout(layout)
      self.goBackButton.clicked.connect(self.reject)

   def clearFields(self):
      self.addressLineEdit.setText('')
      self.messageTextEdit.setPlainText('')
      self.signatureDisplay.setPlainText('')


class MessageSigningWidget(QtWidgets.QWidget):

   def __init__(self, parent=None, main=None):
      super(MessageSigningWidget, self).__init__(parent)
      self.main = main
      signMessageLayout = QtWidgets.QGridLayout()
      self.setMinimumWidth(800)

      # Pick an Address in Row 0 of the grid layout
      addressLabel = QtWidgets.QLabel(self.tr('Sign with Address:'))
      self.addressLineEdit = QtWidgets.QLineEdit()
      self.addressBookButton = createAddrBookButton(self, self.addressLineEdit, None,
                                                    selectMineOnly=True, showLockboxes=False)
      signMessageLayout.addWidget(addressLabel,      0, 0)
      signMessageLayout.addWidget(self.addressLineEdit,  0, 1)
      signMessageLayout.addWidget(self.addressBookButton,  0, 2)

      # Create a message in Row 1
      messageLabel = QtWidgets.QLabel(self.tr("Message to sign:"))
      self.messageTextEdit = QtWidgets.QTextEdit()
      self.messageTextEdit.setAcceptRichText(False)
      self.messageTextEdit.setStyleSheet("font: 9pt \"Courier\";")
      signMessageLayout.addWidget(messageLabel,          1, 0)
      signMessageLayout.addWidget(self.messageTextEdit,  1, 1, 1, 2)

      # Create a row with just a sign message button
      self.bareSigButton = QtWidgets.QPushButton(self.tr('Bare Signature (Bitcoin Core Compatible)'))
      self.base64SigButton = QtWidgets.QPushButton(self.tr('Base64 Signature'))
      self.clearSigButton = QtWidgets.QPushButton(self.tr('Clearsign Signature'))
      sigButtonFrame = makeHorizFrame([self.bareSigButton,\
                                        self.base64SigButton,\
                                        self.clearSigButton,\
                                        'Stretch'])
      signMessageLayout.addWidget(sigButtonFrame,  2, 1, 1, 3)

      # Create a Signature display
      signatureLabel = QtWidgets.QLabel(self.tr('Message Signature:'))
      self.signatureDisplay = QtWidgets.QTextEdit()
      self.signatureDisplay.setReadOnly(True)
      self.signatureDisplay.setStyleSheet("font: 9pt \"Courier\"; background-color: #bbbbbb;")
      signMessageLayout.addWidget(signatureLabel,         3, 0)
      signMessageLayout.addWidget(self.signatureDisplay,  3, 1, 1, 2)

      self.copySignatureButton = QtWidgets.QPushButton(self.tr("Copy Signature"))
      self.clearFieldsButton = QtWidgets.QPushButton(self.tr("Clear All"))
      buttonFrame = makeHorizFrame([self.copySignatureButton, self.clearFieldsButton,'Stretch'])
      signMessageLayout.addWidget(buttonFrame, 4, 1, 1, 3)

      self.setLayout(signMessageLayout)
      self.bareSigButton.clicked.connect(self.bareSignMessage)
      self.base64SigButton.clicked.connect(self.base64SignMessage)
      self.clearSigButton.clicked.connect(self.clearSignMessage)
      self.copySignatureButton.clicked.connect(self.copySignature)
      self.clearFieldsButton.clicked.connect(self.clearFields)

   def getPrivateKeyFromAddrInput(self):
      atype, addr160 = addrStr_to_hash160(str(self.addressLineEdit.text()))
      if atype==P2SHBYTE:
         LOGWARN('P2SH address requested')
         QtWidgets.QMessageBox.critical(self, self.tr('P2SH Address'), \
            self.tr('You are attempting to sign a message with a P2SH address. <br><br>This feature is not supported at the moment'), \
               QtWidgets.QMessageBox.Ok)
         return

      walletId = self.main.getWalletForAddrHash(addr160)
      wallet = self.main.walletMap[walletId]
      if wallet.useEncryption and wallet.isLocked:
         # Target wallet is encrypted...
         unlockdlg = DlgUnlockWallet(wallet, self, self.main, self.tr('Unlock Wallet to Import'))
         if not unlockdlg.exec_():
            QtWidgets.QMessageBox.critical(self, self.tr('Wallet is Locked'), \
               self.tr('Cannot import private keys without unlocking wallet!'), \
               QtWidgets.QMessageBox.Ok)
            return
      return wallet.getAddrObjectForHash(addr160).binPrivKey32_Plain.toBinStr()

   def bareSignMessage(self):
      messageText = str(self.messageTextEdit.toPlainText())
      if not isASCII(messageText):
         QtWidgets.QMessageBox.warning(self, self.tr('Non ASCII Text'), self.tr('Message to sign must be ASCII'), QtWidgets.QMessageBox.Ok)
      else:
         try:
            privateKey = self.getPrivateKeyFromAddrInput()
            if privateKey:
               signature = ASv0(privateKey, messageText)
               self.signatureDisplay.setPlainText(signature['b64-signature'])
            else:
               QtWidgets.QMessageBox.warning(self, self.tr('Private Key Not Known'), self.tr('The private key is not known for this address.'), QtWidgets.QMessageBox.Ok)
         except:
            QtWidgets.QMessageBox.warning(self, self.tr('Invalid Address'), self.tr('The signing address is invalid.'), QtWidgets.QMessageBox.Ok)
            raise
   
   def base64SignMessage(self):
      messageText = str(self.messageTextEdit.toPlainText())
      if not isASCII(messageText):
         QtWidgets.QMessageBox.warning(self, self.tr('Non ASCII Text'), self.tr('Message to sign must be ASCII'), QtWidgets.QMessageBox.Ok)
      else:
         try:
            privateKey = self.getPrivateKeyFromAddrInput()
            if privateKey:
               signature = ASv1B64(self.getPrivateKeyFromAddrInput(), messageText)
               self.signatureDisplay.setPlainText(signature)    
            else:
               QtWidgets.QMessageBox.warning(self, self.tr('Private Key Not Known'), self.tr('The private key is not known for this address.'), QtWidgets.QMessageBox.Ok)
         except:
            QtWidgets.QMessageBox.warning(self, self.tr('Invalid Address'), self.tr('The signing address is invalid.'), QtWidgets.QMessageBox.Ok)
            raise
   
   def clearSignMessage(self):
      messageText = str(self.messageTextEdit.toPlainText())
      if not isASCII(messageText):
         QtWidgets.QMessageBox.warning(self, self.tr('Non ASCII Text'), self.tr('Message to sign must be ASCII'), QtWidgets.QMessageBox.Ok)
      else:
         try:
            privateKey = self.getPrivateKeyFromAddrInput()
         except:
            QtWidgets.QMessageBox.warning(self, self.tr('Invalid Address'), self.tr('The signing address is invalid.'), QtWidgets.QMessageBox.Ok)
            raise
         if privateKey:
            signature = ASv1CS(privateKey, messageText)
            self.signatureDisplay.setPlainText(signature)
         else:
            QtWidgets.QMessageBox.warning(self, self.tr('Private Key Not Known'), self.tr('The private key is not known for this address.'), QtWidgets.QMessageBox.Ok)

   def copySignature(self):
      clipb = QtWidgets.QApplication.clipboard()
      clipb.clear()
      clipb.setText(str(self.signatureDisplay.toPlainText()))

   def clearFields(self):
      self.addressLineEdit.setText('')
      self.messageTextEdit.setPlainText('')
      self.signatureDisplay.setPlainText('')

# Intended to be a base class
class SignatureVerificationWidget(QtWidgets.QWidget):

   def __init__(self, parent=None, main=None):
      super(SignatureVerificationWidget, self).__init__(parent)
      self.main = main
      self.signMessageLayout = QtWidgets.QGridLayout()
      self.setMinimumWidth(800)

      self.verifySignatureButton = QtWidgets.QPushButton(self.tr("Verify Signature"))
      self.clearFieldsButton = QtWidgets.QPushButton(self.tr("Clear All"))
      
      self.lblSigResult = QRichLabel('', doWrap=False)
      buttonFrame = makeHorizFrame([self.verifySignatureButton, self.clearFieldsButton,\
                                    'Stretch', self.lblSigResult])
      self.signMessageLayout.addWidget(buttonFrame, 3, 1, 1, 2)

      self.setLayout(self.signMessageLayout)
      self.verifySignatureButton.clicked.connect(self.verifySignature)
      self.clearFieldsButton.clicked.connect(self.clearFields)

   # To be implemented by child classes
   def verifySignature(self):
      pass
         
   def clearFields(self):
      self.lblSigResult.setText('')
      
   def displayVerifiedBox(self, addrB58, messageString):
      ownerStr = str(self.tr(
      'The owner of the following Bitcoin address...'
      '<br>'
      '<blockquote>'
      '<font face="Courier" size=4 color="#000060"><b>%s</b></font>'
      '</blockquote>'
      '<br>'
      '... has produced a <b><u>valid</u></b> signature for '
      'the following message:<br>' % addrB58))
         
      if addrB58:
         msg = messageString.replace('\r\n','\n')
         msg = '   ' + '<br>   '.join(msg.split('\n'))
         # The user will be able to see the entire message 
         # in the Message Signing/Verification dialog
         msg =  '<br>'.join([line[:60]+ '...'*(len(line)>60) for line in msg.split('<br>')][:12])
         MsgBoxCustom(MSGBOX.Good, self.tr('Verified!'), str(self.tr(
            '%s`'
            '<hr>'
            '<blockquote>'
            '<font face="Courier" color="#000060"><b>%s</b></font>'
            '</blockquote>'
            '<hr><br>'
            '<b>Please</b> make sure that the address above (%s...) matches the '
            'exact address you were expecting.  A valid signature is meaningless '
            'unless it is made '
            'from a recognized address!' % (ownerStr, msg, addrB58[:10]))))
         self.lblSigResult.setText(\
            '<font color="green">Valid Signature by %s</font>' % addrB58)
      else:
         self.displayInvalidSignatureMessage()

   def displayInvalidSignatureMessage(self):
      MsgBoxCustom(MSGBOX.Error, self.tr('Invalid Signature!'), \
                                 self.tr('The supplied signature <b>is not valid</b>!'))
      self.lblSigResult.setText(self.tr('<font color="red">Invalid Signature!</font>'))
      
class BareSignatureVerificationWidget(SignatureVerificationWidget):

   def __init__(self, parent=None, main=None):
      super(BareSignatureVerificationWidget, self).__init__(parent, main)
      # Pick an Address in Row 0 of the grid layout
      addressLabel = QtWidgets.QLabel(self.tr('Signing Address:'))
      self.addressLineEdit = QtWidgets.QLineEdit()
      self.addressBookButton = createAddrBookButton(self, self.addressLineEdit, None,
                                                    selectMineOnly=True, showLockboxes=False)
      self.signMessageLayout.addWidget(addressLabel,      0, 0)
      self.signMessageLayout.addWidget(self.addressLineEdit,  0, 1)
      self.signMessageLayout.addWidget(self.addressBookButton,  0, 2)
      
      # Create a message text box
      messageLabel = QtWidgets.QLabel(self.tr("Signed Message:"))
      self.messageTextEdit = QtWidgets.QTextEdit()
      self.messageTextEdit.setAcceptRichText(False)
      self.messageTextEdit.setStyleSheet("font: 9pt \"Courier\";")
      self.signMessageLayout.addWidget(messageLabel,          1, 0)
      self.signMessageLayout.addWidget(self.messageTextEdit,  1, 1)
      # Create a Signature display
      signatureLabel = QtWidgets.QLabel(self.tr('Signature:'))
      self.signatureTextEdit = QtWidgets.QTextEdit()
      self.signatureTextEdit.setStyleSheet("font: 9pt \"Courier\";")
      self.signMessageLayout.addWidget(signatureLabel,         2, 0)
      self.signMessageLayout.addWidget(self.signatureTextEdit,  2, 1)
      
   def verifySignature(self):
      messageString = str(self.messageTextEdit.toPlainText())
      try:
         addrB58 = verifySignature(str(self.signatureTextEdit.toPlainText()), \
                         messageString, 'v0', ord(ADDRBYTE))
         if addrB58 == str(self.addressLineEdit.text()):
            self.displayVerifiedBox(addrB58, messageString)
         else:
            self.displayInvalidSignatureMessage()
      except:   
         self.displayInvalidSignatureMessage()
         raise

   def clearFields(self):
      super(BareSignatureVerificationWidget, self).clearFields()
      self.addressLineEdit.setText('')
      self.messageTextEdit.setPlainText('')
      self.signatureTextEdit.setPlainText('')

class SignedMessageBlockVerificationWidget(SignatureVerificationWidget):

   def __init__(self, parent=None, main=None):
      super(SignedMessageBlockVerificationWidget, self).__init__(parent, main)
      # Create a Signature display
      signatureLabel = QtWidgets.QLabel(self.tr('Signed Message Block:'))
      self.signedMessageBlockTextEdit = QtWidgets.QTextEdit()
      self.signedMessageBlockTextEdit.setStyleSheet("font: 9pt \"Courier\";")
      self.signedMessageBlockTextEdit.setAcceptRichText(False)
      self.signMessageLayout.addWidget(signatureLabel,         0, 0)
      self.signMessageLayout.addWidget(self.signedMessageBlockTextEdit,  0, 1)

      # Create a message in Row 1
      messageLabel = QtWidgets.QLabel(self.tr("Message:"))
      self.messageTextEdit = QtWidgets.QTextEdit()
      self.messageTextEdit.setAcceptRichText(False)
      self.messageTextEdit.setReadOnly(True)
      self.messageTextEdit.setStyleSheet("font: 9pt \"Courier\"; background-color: #bbbbbb;")
      self.signMessageLayout.addWidget(messageLabel,          1, 0)
      self.signMessageLayout.addWidget(self.messageTextEdit,  1, 1, 1, 2)
      
      
   def verifySignature(self):
      try:
         sig, msg = readSigBlock(str(self.signedMessageBlockTextEdit.toPlainText()))
         addrB58 = verifySignature(sig, msg, 'v1', ord(ADDRBYTE) )
         self.displayVerifiedBox(addrB58, msg)
         self.messageTextEdit.setPlainText(msg)
      except:   
         self.displayInvalidSignatureMessage()
         raise
         
   def clearFields(self):
      super(SignedMessageBlockVerificationWidget, self).clearFields()
      self.signedMessageBlockTextEdit.setPlainText('')
      self.messageTextEdit.setPlainText('')
