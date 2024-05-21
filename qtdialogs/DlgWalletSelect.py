##############################################################################
#                                                                            #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                         #
# Distributed under the GNU Affero General Public License (AGPL v3)          #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                       #
#                                                                            #
# Copyright (C) 2016-2022, goatpig                                           #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################

from qtpy import QtWidgets

from qtdialogs.qtdefines import HORIZONTAL
from qtdialogs.ArmoryDialog import ArmoryDialog
from ui.WalletFrames import SelectWalletFrame

################################################################################
class DlgWalletSelect(ArmoryDialog):
   def __init__(self, parent=None, main=None, title='Select Wallet:', descr='',
      firstSelect=None, onlyMyWallets=False, wltIDList=None, atLeast=0):
      super(DlgWalletSelect, self).__init__(parent, main)


      self.balAtLeast = atLeast

      if self.main and len(self.main.walletMap) == 0:
         QtWidgets.QMessageBox.critical(self, self.tr('No Wallets!'),
            self.tr('There are no wallets to select from. '
            'Please create or import a wallet first.'),
            QtWidgets.QMessageBox.Ok)
         self.accept()
         return

      if wltIDList == None:
         wltIDList = list(self.main.walletIDList)

      # Start the layout
      layout = QtWidgets.QVBoxLayout()
      # Expect to set selectedId
      wltFrame = SelectWalletFrame(self, main,
         HORIZONTAL, firstSelect, onlyMyWallets,
         wltIDList, atLeast, self.selectWallet)

      layout.addWidget(wltFrame)
      self.selectedID = wltFrame.selectedID
      buttonBox = QtWidgets.QDialogButtonBox()
      btnAccept = QtWidgets.QPushButton('OK')
      btnCancel = QtWidgets.QPushButton('Cancel')
      btnAccept.clicked.connect(self.accept)
      btnCancel.clicked.connect(self.reject)
      buttonBox.addButton(btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(btnCancel, QtWidgets.QDialogButtonBox.RejectRole)

      layout.addWidget(buttonBox)

      layout.setSpacing(15)
      self.setLayout(layout)
      self.setWindowTitle(self.tr('Select Wallet'))

   def selectWallet(self, wlt, isDoubleClick=False):
      self.selectedID = wlt.uniqueIDB58
      if isDoubleClick:
         self.accept()