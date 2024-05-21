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

from qtdialogs.ArmoryDialog import ArmoryDialog

################################################################################
class DlgQRCodeDisplay(ArmoryDialog):
   def __init__(self, parent, main, dataToQR, descrUp='', descrDown=''):
      super(DlgQRCodeDisplay, self).__init__(parent, main)

      btnDone = QtWidgets.QPushButton('Close')
      self.connect(btnDone, SIGNAL(CLICKED), self.accept)
      frmBtn = makeHorizFrame([STRETCH, btnDone, STRETCH])

      qrDisp = QRCodeWidget(dataToQR, parent=self)
      frmQR = makeHorizFrame([STRETCH, qrDisp, STRETCH])

      lblUp = QRichLabel(descrUp)
      lblUp.setAlignment(QtCore.Qt.AlignVCenter | QtCore.Qt.AlignHCenter)
      lblDn = QRichLabel(descrDown)
      lblDn.setAlignment(QtCore.Qt.AlignVCenter | QtCore.Qt.AlignHCenter)



      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblUp)
      layout.addWidget(frmQR)
      layout.addWidget(lblDn)
      layout.addWidget(HLINE())
      layout.addWidget(frmBtn)

      self.setLayout(layout)

      w1, h1 = relaxedSizeStr(lblUp, descrUp)
      w2, h2 = relaxedSizeStr(lblDn, descrDown)
      self.setMinimumWidth(1.2 * max(w1, w2))