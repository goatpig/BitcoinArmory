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

import os

from qtpy import QtWidgets

from armoryengine.ArmoryUtils import LOGEXCEPT, RightNowStr

from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.DlgProgress import DlgProgress

################################################################################
class DlgReplaceWallet(ArmoryDialog):

   #############################################################################
   def __init__(self, WalletID, parent, main):
      super(DlgReplaceWallet, self).__init__(parent, main)

      lblDesc = QtWidgets.QLabel(self.tr(
         '<b>You already have this wallet loaded!</b><br>'
         'You can choose to:<br>'
         '- Cancel wallet restore operation<br>'
         '- Set new password and fix any errors<br>'
         '- Overwrite old wallet (delete comments & labels)<br>'))

      self.WalletID = WalletID
      self.main = main
      self.Meta = None
      self.output = 0

      self.btnAbort = QtWidgets.QPushButton(self.tr('Cancel'))
      self.btnReplace = QtWidgets.QPushButton(self.tr('Overwrite'))
      self.btnSaveMeta = QtWidgets.QPushButton(self.tr('Merge'))

      self.btnAbort.clicked.connect(self.reject)
      self.btnReplace.clicked.connect(self.Replace)
      self.btnSaveMeta.clicked.connect(self.SaveMeta)

      layoutDlg = QtWidgets.QGridLayout()

      layoutDlg.addWidget(lblDesc,          0, 0, 4, 4)
      layoutDlg.addWidget(self.btnAbort,    4, 0, 1, 1)
      layoutDlg.addWidget(self.btnSaveMeta, 4, 1, 1, 1)
      layoutDlg.addWidget(self.btnReplace,  4, 2, 1, 1)

      self.setLayout(layoutDlg)
      self.setWindowTitle('Wallet already exists')

   #########
   def Replace(self):
      self.output = 1
      self.accept()

   #########
   def SaveMeta(self):
      self.output = 2
      self.accept()
