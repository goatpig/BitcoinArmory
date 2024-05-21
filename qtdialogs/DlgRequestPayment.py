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
class DlgRequestPayment(ArmoryDialog):
   def __init__(self, parent, main, recvAddr, amt=None, msg=''):
      super(DlgRequestPayment, self).__init__(parent, main)


      if isLikelyDataType(recvAddr, DATATYPE.Binary) and len(recvAddr) == 20:
         self.recvAddr = hash160_to_addrStr(recvAddr)
      elif isLikelyDataType(recvAddr, DATATYPE.Base58):
         self.recvAddr = recvAddr
      else:
         raise BadAddressError('Unrecognized address input')


      # Amount
      self.edtAmount = QtWidgets.QLineEdit()
      self.edtAmount.setFont(GETFONT('Fixed'))
      self.edtAmount.setMaximumWidth(relaxedSizeNChar(GETFONT('Fixed'), 13)[0])
      if amt:
         self.edtAmount.setText(coin2str(amt, maxZeros=0))


      # Message:
      self.edtMessage = QtWidgets.QLineEdit()
      self.edtMessage.setMaxLength(128)
      if msg:
         self.edtMessage.setText(msg[:128])

      self.edtMessage.setCursorPosition(0)



      # Address:
      self.edtAddress = QtWidgets.QLineEdit()
      self.edtAddress.setText(self.recvAddr)

      # Link Text:
      self.edtLinkText = QtWidgets.QLineEdit()
      defaultHex = binary_to_hex('Click here to pay for your order!')
      savedHex = TheSettings.getSettingOrSetDefault('DefaultLinkText', defaultHex)
      if savedHex.startswith('FFFFFFFF'):
         # An unfortunate hack until we change our settings storage mechanism
         # See comment in saveLinkText function for details
         savedHex = savedHex[8:]

      linkText = hex_to_binary(savedHex)
      self.edtLinkText.setText(linkText)
      self.edtLinkText.setCursorPosition(0)
      self.edtLinkText.setMaxLength(80)

      qpal = QtGui.QPalette()
      qpal.setColor(QtGui.QPalette.Text, Colors.TextBlue)
      self.edtLinkText.setPalette(qpal)
      edtFont = self.edtLinkText.font()
      edtFont.setUnderline(True)
      self.edtLinkText.setFont(edtFont)



      self.connect(self.edtMessage, SIGNAL('textChanged(QString)'), self.setLabels)
      self.connect(self.edtAddress, SIGNAL('textChanged(QString)'), self.setLabels)
      self.connect(self.edtAmount, SIGNAL('textChanged(QString)'), self.setLabels)
      self.connect(self.edtLinkText, SIGNAL('textChanged(QString)'), self.setLabels)

      self.connect(self.edtMessage, SIGNAL('editingFinished()'), self.updateQRCode)
      self.connect(self.edtAddress, SIGNAL('editingFinished()'), self.updateQRCode)
      self.connect(self.edtAmount, SIGNAL('editingFinished()'), self.updateQRCode)
      self.connect(self.edtLinkText, SIGNAL('editingFinished()'), self.updateQRCode)


      # This is the "output"
      self.lblLink = QRichLabel('')
      self.lblLink.setOpenExternalLinks(True)
      self.lblLink.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse | QtCore.Qt.TextSelectableByKeyboard)
      self.lblLink.setMinimumHeight(3 * tightSizeNChar(self, 1)[1])
      self.lblLink.setAlignment(QtCore.Qt.AlignVCenter | QtCore.Qt.AlignLeft)
      self.lblLink.setContentsMargins(10, 5, 10, 5)
      self.lblLink.setStyleSheet('QtWidgets.QLabel { background-color : %s }' % htmlColor('SlightBkgdDark'))
      frmOut = makeHorizFrame([self.lblLink], QtWidgets.QFrame.Box | QtWidgets.QFrame.Raised)
      frmOut.setLineWidth(1)
      frmOut.setMidLineWidth(5)


      self.lblWarn = QRichLabel('')
      self.lblWarn.setAlignment(QtCore.Qt.AlignVCenter | QtCore.Qt.AlignHCenter)

      self.btnOtherOpt = QtWidgets.QPushButton(self.tr('Other Options >>>'))
      self.btnCopyRich = QtWidgets.QPushButton(self.tr('Copy to Clipboard'))
      self.btnCopyHtml = QtWidgets.QPushButton(self.tr('Copy Raw HTML'))
      self.btnCopyRaw = QtWidgets.QPushButton(self.tr('Copy Raw URL'))
      self.btnCopyAll = QtWidgets.QPushButton(self.tr('Copy All Text'))

      # I never actally got this button working right...
      self.btnCopyRich.setVisible(True)
      self.btnOtherOpt.setCheckable(True)
      self.btnCopyAll.setVisible(False)
      self.btnCopyHtml.setVisible(False)
      self.btnCopyRaw.setVisible(False)
      frmCopyBtnStrip = makeHorizFrame([ \
                                        self.btnCopyRich, \
                                        self.btnOtherOpt, \
                                        self.btnCopyHtml, \
                                        self.btnCopyRaw, \
                                        STRETCH, \
                                        self.lblWarn])
                                        # self.btnCopyAll, \

      self.connect(self.btnCopyRich, SIGNAL(CLICKED), self.clickCopyRich)
      self.connect(self.btnOtherOpt, SIGNAL('toggled(bool)'), self.clickOtherOpt)
      self.connect(self.btnCopyRaw, SIGNAL(CLICKED), self.clickCopyRaw)
      self.connect(self.btnCopyHtml, SIGNAL(CLICKED), self.clickCopyHtml)
      self.connect(self.btnCopyAll, SIGNAL(CLICKED), self.clickCopyAll)

      lblDescr = QRichLabel(\
         self.tr('Create a clickable link that you can copy into email or webpage to '
         'request a payment.   If the user is running a Bitcoin program '
         'that supports "bitcoin:" links, that program will open with '
         'all this information pre-filled after they click the link.'))

      lblDescr.setContentsMargins(5, 5, 5, 5)
      frmDescr = makeHorizFrame([lblDescr], STYLE_SUNKEN)


      ttipPreview = self.main.createToolTipWidget(\
         self.tr('The following Bitcoin desktop applications <i>try</i> to '
         'register themselves with your computer to handle "bitcoin:" '
         'links: Armory, Multibit, Electrum'))
      ttipLinkText = self.main.createToolTipWidget(\
         self.tr('This is the text to be shown as the clickable link.  It should '
         'usually begin with "Click here..." to reaffirm to the user it is '
         'is clickable.'))
      ttipAmount = self.main.createToolTipWidget(\
         self.tr('All amounts are specifed in BTC'))
      ttipAddress = self.main.createToolTipWidget(\
         self.tr('The person clicking the link will be sending bitcoins to this address'))
      ttipMessage = self.main.createToolTipWidget(\
         self.tr('This will be pre-filled as the label/comment field '
         'after the user clicks the link. They '
         'can modify it if desired, but you can '
         'provide useful info such as contact details, order number, '
         'etc, as convenience to them.'))


      btnClose = QtWidgets.QPushButton(self.tr('Close'))
      self.connect(btnClose, SIGNAL(CLICKED), self.accept)


      frmEntry = QtWidgets.QFrame()
      frmEntry.setFrameStyle(STYLE_SUNKEN)
      layoutEntry = QtWidgets.QGridLayout()
      i = 0
      layoutEntry.addWidget(QRichLabel(self.tr('<b>Link Text:</b>')), i, 0)
      layoutEntry.addWidget(self.edtLinkText, i, 1)
      layoutEntry.addWidget(ttipLinkText, i, 2)

      i += 1
      layoutEntry.addWidget(QRichLabel(self.tr('<b>Address (yours):</b>')), i, 0)
      layoutEntry.addWidget(self.edtAddress, i, 1)
      layoutEntry.addWidget(ttipAddress, i, 2)

      i += 1
      layoutEntry.addWidget(QRichLabel(self.tr('<b>Request (BTC):</b>')), i, 0)
      layoutEntry.addWidget(self.edtAmount, i, 1)

      i += 1
      layoutEntry.addWidget(QRichLabel(self.tr('<b>Label:</b>')), i, 0)
      layoutEntry.addWidget(self.edtMessage, i, 1)
      layoutEntry.addWidget(ttipMessage, i, 2)
      frmEntry.setLayout(layoutEntry)


      lblOut = QRichLabel(self.tr('Copy and paste the following text into email or other document:'))
      frmOutput = makeVertFrame([lblOut, frmOut, frmCopyBtnStrip], STYLE_SUNKEN)
      frmOutput.layout().setStretch(0, 1)
      frmOutput.layout().setStretch(1, 1)
      frmOutput.layout().setStretch(2, 0)
      frmClose = makeHorizFrame([STRETCH, btnClose])

      self.qrStackedDisplay = QStackedWidget()
      self.qrStackedDisplay.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Preferred)
      self.qrWaitingLabel = QRichLabel(self.tr('Creating QR Code Please Wait'), doWrap=False, hAlign=QtCore.Qt.AlignHCenter)
      self.qrStackedDisplay.addWidget(self.qrWaitingLabel)
      self.qrURI = QRCodeWidget('', parent=self)
      self.qrStackedDisplay.addWidget(self.qrURI)
      lblQRDescr = QRichLabel(self.tr('This QR code contains address <b>and</b> the '
                              'other payment information shown to the left.'), doWrap=True)

      lblQRDescr.setAlignment(QtCore.Qt.AlignTop | QtCore.Qt.AlignHCenter)
      frmQR = makeVertFrame([self.qrStackedDisplay, STRETCH, lblQRDescr, STRETCH], STYLE_SUNKEN)
      frmQR.layout().setStretch(0, 0)
      frmQR.layout().setStretch(1, 0)
      frmQR.layout().setStretch(2, 1)

      frmQR.setMinimumWidth(MAX_QR_SIZE)
      self.qrURI.setMinimumHeight(MAX_QR_SIZE)

      dlgLayout = QtWidgets.QGridLayout()

      dlgLayout.addWidget(frmDescr, 0, 0, 1, 2)
      dlgLayout.addWidget(frmEntry, 1, 0, 1, 1)
      dlgLayout.addWidget(frmOutput, 2, 0, 1, 1)
      dlgLayout.addWidget(HLINE(), 3, 0, 1, 2)
      dlgLayout.addWidget(frmClose, 4, 0, 1, 2)

      dlgLayout.addWidget(frmQR, 1, 1, 2, 1)

      dlgLayout.setRowStretch(0, 0)
      dlgLayout.setRowStretch(1, 0)
      dlgLayout.setRowStretch(2, 1)
      dlgLayout.setRowStretch(3, 0)
      dlgLayout.setRowStretch(4, 0)


      self.setLabels()
      self.prevURI = ''
      self.closed = False  # kind of a hack to end the update loop
      self.setLayout(dlgLayout)
      self.setWindowTitle(self.tr('Create Payment Request Link'))

      self.callLater(1, self.periodicUpdate)

      hexgeom = str(self.main.settings.get('PayRequestGeometry'))
      if len(hexgeom) > 0:
         geom = QtCore.QByteArray.fromHex(hexgeom)
         self.restoreGeometry(geom)
      self.setMinimumSize(750, 500)


   def saveLinkText(self):
      linktext = str(self.edtLinkText.text()).strip()
      if len(linktext) > 0:
         # TODO:  We desperately need a new settings file format -- the one
         #        we use was more of an experiment in how quickly I could
         #        create a simple settings file, but it has quirky behavior
         #        that makes cryptic hacks like below necessary.  Simply put,
         #        if the hex of the text is all digits (no hex A-F), then
         #        the settings file will read the value as a long int instead
         #        of a string.  We add 8 F's to make sure it's interpretted
         #        as a hex string, but someone looking at the file wouldn't
         #        mistake it for meaningful data.  We remove it upon reading
         #        the value from the settings file.
         hexText = 'FFFFFFFF'+binary_to_hex(linktext)
         TheSettings.set('DefaultLinkText', hexText)


   #############################################################################
   def saveGeometrySettings(self):
      TheSettings.set('PayRequestGeometry', self.saveGeometry().toHex())

   #############################################################################
   def closeEvent(self, event):
      self.saveGeometrySettings()
      self.saveLinkText()
      super(DlgRequestPayment, self).closeEvent(event)

   #############################################################################
   def accept(self, *args):
      self.saveGeometrySettings()
      self.saveLinkText()
      super(DlgRequestPayment, self).accept(*args)

   #############################################################################
   def reject(self, *args):
      self.saveGeometrySettings()
      super(DlgRequestPayment, self).reject(*args)


   #############################################################################
   def setLabels(self):

      lastTry = ''
      try:
         # The
         lastTry = self.tr('Amount')
         amtStr = str(self.edtAmount.text()).strip()
         if len(amtStr) == 0:
            amt = None
         else:
            amt = str2coin(amtStr)

            if amt > MAX_SATOSHIS:
               amt = None

         lastTry = self.tr('Message')
         msgStr = str(self.edtMessage.text()).strip()
         if len(msgStr) == 0:
            msgStr = None

         lastTry = self.tr('Address')
         addr = str(self.edtAddress.text()).strip()
         if not checkAddrStrValid(addr):
            raise

         errorIn = self.tr('Inputs')
         # must have address, maybe have amount and/or message
         self.rawURI = createBitcoinURI(addr, amt, msgStr)
      except:
         self.lblWarn.setText(self.tr('<font color="red">Invalid %s</font>' % lastTry))
         self.btnCopyRaw.setEnabled(False)
         self.btnCopyHtml.setEnabled(False)
         self.btnCopyAll.setEnabled(False)
         # self.lblLink.setText('<br>'.join(str(self.lblLink.text()).split('<br>')[1:]))
         self.lblLink.setEnabled(False)
         self.lblLink.setTextInteractionFlags(QtCore.Qt.NoTextInteraction)
         return

      self.lblLink.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse | \
                                           QtCore.Qt.TextSelectableByKeyboard)

      self.rawHtml = '<a href="%s">%s</a>' % (self.rawURI, str(self.edtLinkText.text()))
      self.lblWarn.setText('')
      self.dispText = self.rawHtml[:]
      self.dispText += '<br>'
      self.dispText += self.tr('If clicking on the line above does not work, use this payment info:')
      self.dispText += '<br>'
      self.dispText += self.tr('<b>Pay to</b>:\t%s<br>' % addr)
      if amt:
         self.dispText += self.tr('<b>Amount</b>:\t%s BTC<br>' % coin2str(amt, maxZeros=0).strip())
      if msgStr:
         self.dispText += self.tr('<b>Message</b>:\t%s<br>' % msgStr)
      self.lblLink.setText(self.dispText)

      self.lblLink.setEnabled(True)
      self.btnCopyRaw.setEnabled(True)
      self.btnCopyHtml.setEnabled(True)
      self.btnCopyAll.setEnabled(True)

      # Plain text to copy to clipboard as "text/plain"
      self.plainText = str(self.edtLinkText.text()) + '\n'
      self.plainText += self.tr('If clicking on the line above does not work, use this payment info:\n')
      self.plainText += self.tr('Pay to:  %s' % addr)
      if amt:
         self.plainText += self.tr('\nAmount:  %s BTC' % coin2str(amt, maxZeros=0).strip())
      if msgStr:
         self.plainText += self.tr('\nMessage: %s' % msgStr)
      self.plainText += '\n'

      # The rich-text to copy to the clipboard, as "text/html"
      self.clipText = self.tr('<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN" '
            '"http://www.w3.org/TR/REC-html40/strict.dtd"> '
            '<html><head><meta name="qrichtext" content="1" />'
            '<meta http-equiv="Content-Type" content="text/html; '
            'charset=utf-8" /><style type="text/css"> p, li '
            '{ white-space: pre-wrap; } </style></head><body>'
            '<p style=" margin-top:0px; margin-bottom:0px; margin-left:0px; '
            'margin-right:0px; -qt-block-indent:0; text-indent:0px;">'
            '<!--StartFragment--><a href="%s">'
            '<span style=" text-decoration: underline; color:#0000ff;">'
            '%s</span></a><br />'
            'If clicking on the line above does not work, use this payment info:'
            '<br /><span style=" font-weight:600;">Pay to</span>: %s' % (self.rawURI, str(self.edtLinkText.text()), addr))
      if amt:
         self.clipText += self.tr('<br /><span style=" font-weight:600;">Amount'
                           '</span>: %s' % coin2str(amt, maxZeros=0))
      if msgStr:
         self.clipText += self.tr('<br /><span style=" font-weight:600;">Message'
                           '</span>: %s' % msgStr)
      self.clipText += '<!--EndFragment--></p></body></html>'

   def periodicUpdate(self, nsec=1):
      if not self.closed:
         self.updateQRCode()
         self.callLater(nsec, self.periodicUpdate)

   def updateQRCode(self, e=None):
      if not self.prevURI == self.rawURI:
         self.qrStackedDisplay.setCurrentWidget(self.qrWaitingLabel)
         self.repaint()
         self.qrURI.setAsciiData(self.rawURI)
         self.qrURI.setPreferredSize(MAX_QR_SIZE - 10, 'max')
         self.qrStackedDisplay.setCurrentWidget(self.qrURI)
         self.repaint()
      self.prevURI = self.rawURI

   def clickCopyRich(self):
      clipb = QtWidgets.QApplication.clipboard()
      clipb.clear()
      qmd = QMimeData()
      if OS_WINDOWS:
         qmd.setText(self.plainText)
         qmd.setHtml(self.clipText)
      else:
         prefix = '<meta http-equiv="content-type" content="text/html; charset=utf-8">'
         qmd.setText(self.plainText)
         qmd.setHtml(prefix + self.dispText)
      clipb.setMimeData(qmd)
      self.lblWarn.setText(self.tr('<i>Copied!</i>'))



   def clickOtherOpt(self, boolState):
      self.btnCopyHtml.setVisible(boolState)
      self.btnCopyRaw.setVisible(boolState)

      if boolState:
         self.btnOtherOpt.setText(self.tr('Hide Buttons <<<'))
      else:
         self.btnOtherOpt.setText(self.tr('Other Options >>>'))

   def clickCopyRaw(self):
      clipb = QtWidgets.QApplication.clipboard()
      clipb.clear()
      clipb.setText(self.rawURI)
      self.lblWarn.setText(self.tr('<i>Copied!</i>'))

   def clickCopyHtml(self):
      clipb = QtWidgets.QApplication.clipboard()
      clipb.clear()
      clipb.setText(self.rawHtml)
      self.lblWarn.setText(self.tr('<i>Copied!</i>'))

   def clickCopyAll(self):
      clipb = QtWidgets.QApplication.clipboard()
      clipb.clear()
      qmd = QMimeData()
      qmd.setHtml(self.dispText)
      clipb.setMimeData(qmd)
      self.lblWarn.setText(self.tr('<i>Copied!</i>'))