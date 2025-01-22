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

from qtdialogs.qtdefines import ArmoryDialog

################################################################################
class DlgUriCopyAndPaste(ArmoryDialog):
   def __init__(self, parent, main):
      super(DlgUriCopyAndPaste, self).__init__(parent, main)

      self.uriDict = {}
      lblDescr = QRichLabel(self.tr('Copy and paste a raw bitcoin URL string here.  '
                            'A valid string starts with "bitcoin:" followed '
                            'by a bitcoin address.'
                            '<br><br>'
                            'You should use this feature if there is a "bitcoin:" '
                            'link in a webpage or email that does not load Armory '
                            'when you click on it.  Instead, right-click on the '
                            'link and select "Copy Link Location" then paste it '
                            'into the box below. '))

      lblShowExample = QtWidgets.QLabel()
      lblShowExample.setPixmap(QtGui.QPixmap(':/armory_rightclickcopy.png'))

      self.txtUriString = QtWidgets.QLineEdit()
      self.txtUriString.setFont(GETFONT('Fixed', 8))

      self.btnOkay = QtWidgets.QPushButton(self.tr('Done'))
      self.btnCancel = QtWidgets.QPushButton(self.tr('Cancel'))
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnOkay, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)

      self.btnOkay.clicked.connect(self.clickedOkay)
      self.btnCancel.clicked.connect(self.reject)
      frmImg = makeHorizFrame([STRETCH, lblShowExample, STRETCH])

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblDescr)
      layout.addWidget(HLINE())
      layout.addWidget(frmImg)
      layout.addWidget(HLINE())
      layout.addWidget(self.txtUriString)
      layout.addWidget(buttonBox)
      self.setLayout(layout)

   def clickedOkay(self):
      uriStr = str(self.txtUriString.text())
      self.uriDict = self.main.parseUriLink(uriStr, 'enter')
      if len(self.uriDict.keys()) > 0:
         self.accept()
