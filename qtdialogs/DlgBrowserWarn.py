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

from qtdialogs.ArmoryDialog import ArmoryDialog

################################################################################
class DlgBrowserWarn(ArmoryDialog):
   def __init__(self, link, parent=None, main=None):
      super(DlgBrowserWarn, self).__init__(parent, main)

      self.link = link
      self.btnCancel = QtWidgets.QPushButton("Cancel")
      self.btnCancel.clicked.connect(self.cancel)
      self.btnContinue = QtWidgets.QPushButton("Continue")
      self.btnContinue.clicked.connect(self.accept)
      btnBox = makeHorizFrame([STRETCH, self.btnCancel, self.btnContinue])

      lblWarn = QRichLabel(self.tr('Your default browser will now open and go to the following link: %s. Are you sure you want to proceed?' % self.link))

      dlgLayout = QtWidgets.QVBoxLayout()
      frmAll = makeVertFrame([lblWarn, btnBox])

      dlgLayout.addWidget(frmAll)
      self.setLayout(dlgLayout)
      self.setWindowTitle('Warning: Opening Browser')

   def cancel(self):
      super(DlgBrowserWarn, self).reject()

   def accept(self):
      import webbrowser
      webbrowser.open(self.link)
      super(DlgBrowserWarn, self).accept()