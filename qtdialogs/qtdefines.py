##############################################################################
#                                                                            #
# Copyright (C) 2011-2015, Armory Technologies, Inc.                         #
# Distributed under the GNU Affero General Public License (AGPL v3)          #
# See LICENSE or http://www.gnu.org/licenses/agpl.html                       #
#                                                                            #
# Copyright (C) 2016-2025, goatpig                                           #
#  Distributed under the MIT license                                         #
#  See LICENSE-MIT or https://opensource.org/licenses/MIT                    #
#                                                                            #
##############################################################################
import os
import struct
import traceback
from tempfile import mkstemp

from qtpy import QtCore, QtGui, QtWidgets

# Capture the original QColor class before any downstream utilities
# alter QtGui.QColor
BaseQColor = QtGui.QColor

from armoryengine.ArmoryUtils import enum, ARMORY_HOME_DIR, OS_MACOSX, \
   USE_TESTNET, USE_REGTEST, OS_WINDOWS, coin2str, int_to_hex, toBytes, \
   hex_to_binary, LOGERROR, toUnicode
from armoryengine.BinaryUnpacker import BinaryUnpacker, UINT8, UINT16
from armoryengine.WalletUtils import WalletTypes

from armorycolors import Colors, htmlColor
from ui.QrCodeMatrix import CreateQRMatrix

SETTINGS_PATH   = os.path.join(ARMORY_HOME_DIR, 'ArmorySettings.txt')
USERMODE        = enum('Standard', 'Advanced', 'Expert')
SATOSHIMODE     = enum('Auto', 'User')
NETWORKMODE     = enum('Offline', 'Full', 'Disconnected')
WLTFIELDS       = enum('Name', 'Descr', 'WltID', 'NumAddr', 'Secure',
   'BelongsTo', 'Crypto', 'Time', 'Mem', 'Version')
MSGBOX          = enum('Good','Info', 'Question', 'Warning',
   'Critical', 'Error')
DASHBTNS        = enum('Close', 'Browse', 'Settings')
WLTLISTCOLS     = enum('Checkbox', 'WalletID', 'Filename', 'Type', 'Action')

STYLE_SUNKEN = QtWidgets.QFrame.Box | QtWidgets.QFrame.Sunken
STYLE_RAISED = QtWidgets.QFrame.Box | QtWidgets.QFrame.Raised
STYLE_PLAIN  = QtWidgets.QFrame.Box | QtWidgets.QFrame.Plain
STYLE_STYLED = QtWidgets.QFrame.StyledPanel | QtWidgets.QFrame.Raised
STYLE_NONE   = QtWidgets.QFrame.NoFrame
VERTICAL = 'vertical'
HORIZONTAL = 'horizontal'
CHANGE_ADDR_DESCR_STRING = '[[ Change received ]]'

STRETCH = 'Stretch'
MIN_PASSWD_WIDTH = lambda obj: tightSizeStr(obj, '*' * 16)[0]

# Keep track of dialogs and wizard that are executing
runningDialogsList = []

def AddToRunningDialogsList(func):
   def wrapper(*args, **kwargs):
      runningDialogsList.append(args[0])
      result = func(*args, **kwargs)
      runningDialogsList.remove(args[0])
      return result
   return wrapper

################################################################################
def getWalletTypeStr(wtype):
   """Return human-readable string for wallet type enum."""
   tr = lambda s: QtWidgets.QApplication.translate('WalletTypes', s)
   if wtype == WalletTypes.Offline:
      return tr('Offline')
   elif wtype == WalletTypes.WatchOnly:
      return tr('Watching-Only')
   elif wtype == WalletTypes.Crypt:
      return tr('Encrypted')
   elif wtype == WalletTypes.Plain:
      return tr('No Encryption')
   raise ValueError(f"Unknown wallet type: {wtype}")

################################################################################
# Shared UI constants and styles for dialogs and widgets

# Layout spacings and margins for consistent UI
UI_DIALOG_SPACING = 8
UI_FRAME_MARGIN = 24
UI_FRAME_PADDING = 16
UI_BUTTON_SPACING = 8
UI_GRID_SPACING = 8

# Base dialog/tab styles derived from theme colors
UI_STYLE_DIALOG_BASE = """
   QDialog {
      background-color: %(bg)s;
   }
   QTabWidget::pane {
      border: none;
      background-color: %(bg)s;
   }
   QTabBar::tab {
      background-color: %(tab_bg)s;
      color: %(fg)s;
      border: none;
      padding: 8px 16px;
      min-width: 100px;
   }
   QTabBar::tab:selected {
      background-color: %(tab_sel)s;
      color: %(fg)s;
   }
   QTabBar::tab:hover:!selected {
      background-color: %(tab_hover)s;
   }
""" % {
   'bg': htmlColor('Background'),
   'fg': htmlColor('Foreground'),
   'tab_bg': htmlColor('SlightBkgdDark'),
   'tab_sel': htmlColor('SlightBkgdLight'),
   'tab_hover': htmlColor('SlightBkgdLight')
}

def applyDialogBaseStyle(widget):
   widget.setStyleSheet(UI_STYLE_DIALOG_BASE)

# Buttons
UI_STYLE_BUTTON_STANDARD = """
   QPushButton {
      background-color: %(bg)s;
      color: %(fg)s;
      border: 1px solid %(border)s;
      border-radius: 2px;
      padding: 6px 12px;
   }
   QPushButton:hover {
      background-color: %(hover)s;
   }
   QPushButton:pressed {
      background-color: %(pressed)s;
   }
   QPushButton:disabled {
      background-color: %(bg)s;
      color: %(fg_disabled)s;
      border: 1px solid %(border_disabled)s;
   }
""" % {
   'bg': htmlColor('SlightBkgdDark'),
   'fg': htmlColor('Foreground'),
   'border': htmlColor('Mid'),
   'hover': htmlColor('SlightBkgdLight'),
   'pressed': htmlColor('SlightBkgdDark'),
   'fg_disabled': htmlColor('DisableFG'),
   'border_disabled': htmlColor('SlightBkgdDark')
}

UI_STYLE_BUTTON_DIALOG = """
   QPushButton {
      background-color: %(bg)s;
      color: %(fg)s;
      border: 1px solid %(border)s;
      border-radius: 2px;
      padding: 6px 16px;
      min-width: 100px;
   }
   QPushButton:hover {
      background-color: %(hover)s;
   }
   QPushButton:pressed {
      background-color: %(pressed)s;
   }
""" % {
   'bg': htmlColor('SlightBkgdDark'),
   'fg': htmlColor('Foreground'),
   'border': htmlColor('Mid'),
   'hover': htmlColor('SlightBkgdLight'),
   'pressed': htmlColor('SlightBkgdDark')
}

# Inputs
UI_STYLE_INPUT = """
   QLineEdit {
      background-color: %(bg)s;
      color: %(fg)s;
      border: 1px solid %(border)s;
      border-radius: 2px;
      padding: 6px;
   }
   QLineEdit:disabled {
      background-color: %(bg)s;
      color: %(fg_disabled)s;
      border: 1px solid %(border_disabled)s;
      border-radius: 2px;
      padding: 6px;
   }
""" % {
   'bg': htmlColor('SlightBkgdDark'),
   'fg': htmlColor('Foreground'),
   'border': htmlColor('Mid'),
   'fg_disabled': htmlColor('DisableFG'),
   'border_disabled': htmlColor('SlightBkgdDark')
}

# Combobox
UI_STYLE_COMBOBOX = """
   QComboBox {
      background-color: %(bg)s;
      color: %(fg)s;
      border: 1px solid %(border)s;
      border-radius: 2px;
      padding: 6px;
   }
""" % {
   'bg': htmlColor('SlightBkgdDark'),
   'fg': htmlColor('Foreground'),
   'border': htmlColor('Mid'),
   'bg_drop': htmlColor('SlightBkgdDark')
}

# Frames
UI_STYLE_FRAME = """
   QFrame {
      background-color: %(bg)s;
      border: 1px solid %(border)s;
      border-radius: 4px;
   }
   QLabel {
      border: none;
      background: transparent;
      padding: 0px;
      margin: 0px;
   }
""" % {
   'bg': htmlColor('SlightBkgdDark'),
   'border': htmlColor('Mid')
}

# Tree view
UI_STYLE_TREEWIDGET = """
   QTreeWidget {
      background: %(bg)s;
      border: 1px solid %(border)s;
      color: %(fg)s;
      show-decoration-selected: 0;
   }
   QTreeWidget::item {
      height: 30px;
      border: none;
      padding: 4px;
   }
   QTreeWidget::item:hover {
      background: %(hover)s;
   }
""" % {
   'bg': htmlColor('SlightBkgdDark'),
   'border': htmlColor('Mid'),
   'fg': htmlColor('Foreground'),
   'hover': htmlColor('SlightBkgdLight')
}

# Shared UI helper widgets
def createStyledLabel(text, color='Foreground'):
   lbl = QtWidgets.QLabel(text)
   try:
      if isinstance(color, str) and not color.startswith('#'):
         lbl.setStyleSheet('color: %s;' % htmlColor(color))
      else:
         lbl.setStyleSheet('color: %s;' % color)
   except Exception:
      lbl.setStyleSheet('color: %s;' % htmlColor('Foreground'))
   return lbl

def createButtonLayout(*buttons):
   layout = QtWidgets.QHBoxLayout()
   layout.setSpacing(UI_BUTTON_SPACING)
   layout.addStretch(1)
   for b in buttons:
      layout.addWidget(b)
   return layout

def createInputField(width=None, style=None):
   field = QtWidgets.QLineEdit()
   if width:
      field.setFixedWidth(width)
   field.setStyleSheet(style if style else UI_STYLE_INPUT)
   return field

def createStyledButton(text, width=None, style=None):
   btn = QtWidgets.QPushButton(text)
   if width:
      btn.setFixedWidth(width)
   btn.setStyleSheet(style if style else UI_STYLE_BUTTON_STANDARD)
   return btn

class ArmoryComboBox(QtWidgets.QComboBox):
   def paintEvent(self, event):
      # Only default painting. Arrow will be drawn by ComboBoxStyle
      # to avoid artifacts
      super(ArmoryComboBox, self).paintEvent(event)

def createStyledCombo(width=None, style=None):
   combo = ArmoryComboBox()
   if width:
      combo.setFixedWidth(width)
   combo.setStyleSheet(style if style else UI_STYLE_COMBOBOX)
   return combo

class ComboBoxStyle(QtWidgets.QProxyStyle):
   def drawComplexControl(self, control, option, painter, widget=None):
      if control == QtWidgets.QStyle.CC_ComboBox:
         # Let base style draw the control first
         super().drawComplexControl(control, option, painter, widget)

         # Then paint a clear arrow indicator in the arrow subcontrol rect
         arrow_rect = self.subControlRect(
            QtWidgets.QStyle.CC_ComboBox,
            option,
            QtWidgets.QStyle.SC_ComboBoxArrow,
            widget,
         )
         if not arrow_rect.isValid():
            return
         painter.save()
         try:
            painter.setRenderHint(QtGui.QPainter.Antialiasing, True)
         except Exception:
            pass
         # Mask the native arrow background to avoid any overlap artifacts
         bg = BaseQColor(htmlColor('SlightBkgdDark'))
         painter.fillRect(arrow_rect, bg)

         color = BaseQColor(htmlColor('Foreground'))
         painter.setPen(QtCore.Qt.NoPen)
         painter.setBrush(color)

         size = max(6, int(min(arrow_rect.width(), arrow_rect.height()) * 0.35))
         cx, cy = arrow_rect.center().x(), arrow_rect.center().y()
         tri = QtGui.QPolygonF([
            QtCore.QPointF(cx - size, cy - size * 0.5),
            QtCore.QPointF(cx + size, cy - size * 0.5),
            QtCore.QPointF(cx,        cy + size * 0.6),
         ])
         painter.drawPolygon(tri)
         painter.restore()
         return
      else:
         super().drawComplexControl(control, option, painter, widget)

   def drawPrimitive(self, element, option, painter, widget=None):
      if element == QtWidgets.QStyle.PE_IndicatorArrowDown:
         # Draw a high-contrast down arrow so it is clearly visible
         # on dark themes
         rect = option.rect.adjusted(0, 0, -2, -2)
         painter.save()
         try:
            painter.setRenderHint(QtGui.QPainter.Antialiasing, True)
         except Exception:
            pass
         color = BaseQColor(htmlColor('Foreground'))
         painter.setPen(QtCore.Qt.NoPen)
         painter.setBrush(color)

         size = max(6, int(min(rect.width(), rect.height()) * 0.35))
         cx, cy = rect.center().x(), rect.center().y()
         tri = QtGui.QPolygonF([
            QtCore.QPointF(cx - size, cy - size * 0.5),
            QtCore.QPointF(cx + size, cy - size * 0.5),
            QtCore.QPointF(cx,        cy + size * 0.6),
         ])
         painter.drawPolygon(tri)
         painter.restore()
         return
      else:
         super().drawPrimitive(element, option, painter, widget)

def HLINE(style=QtWidgets.QFrame.Plain):
   qf = QtWidgets.QFrame()
   qf.setFrameStyle(QtWidgets.QFrame.HLine | style)
   return qf

def VLINE(style=QtWidgets.QFrame.Plain):
   qf = QtWidgets.QFrame()
   qf.setFrameStyle(QtWidgets.QFrame.VLine | style)
   return qf

# Setup fixed-width and var-width fonts
def GETFONT(ftype, sz=10, bold=False, italic=False):
   fnt = None
   if ftype.lower().startswith('fix'):
      if OS_WINDOWS:
         fnt = QtGui.QFont("Courier", sz)
      elif OS_MACOSX:
         fnt = QtGui.QFont("Menlo", sz)
      else:
         fnt = QtGui.QFont("DejaVu Sans Mono", sz)
   elif ftype.lower().startswith('var'):
      if OS_MACOSX:
         fnt = QtGui.QFont("Lucida Grande", sz)
      else:
         fnt = QtGui.QFont("Verdana", sz)
      #if OS_WINDOWS:
         #fnt = QtGui.QFont("Tahoma", sz)
      #else:
         #fnt = QtGui.QFont("Sans", sz)
   elif ftype.lower().startswith('money'):
      if OS_WINDOWS:
         fnt = QtGui.QFont("Courier", sz)
      elif OS_MACOSX:
         fnt = QtGui.QFont("Menlo", sz)
      else:
         fnt = QtGui.QFont("DejaVu Sans Mono", sz)
   else:
      fnt = QtGui.QFont(ftype, sz)

   if bold:
      fnt.setWeight(QtGui.QFont.Bold)

   if italic:
      fnt.setItalic(True)

   return fnt

def UnicodeErrorBox(parent):
   QtWidgets.QMessageBox.warning(parent, 'ASCII Error', \
      toUnicode('Armory does not currently support non-ASCII characters in '
      'most text fields (like \xc2\xa3\xc2\xa5\xc3\xa1\xc3\xb6\xc3\xa9).  '
      'Please use only letters found '
      'on an English(US) keyboard.  This will be fixed in an upcoming '
      'release'), QtWidgets.QMessageBox.Ok)

#######
def UserModeStr(parent, mode):
   if mode==USERMODE.Standard:
      return parent.tr('Standard User')
   elif mode==USERMODE.Advanced:
      return parent.tr('Advanced User')
   elif mode==USERMODE.Expert:
      return parent.tr('Expert User')

#######
def tightSizeNChar(obj, nChar):
   """
   Approximates the size of a row text of mixed characters

   This is only aproximate, since variable-width fonts will vary
   depending on the specific text
   """

   try:
      fm = QtGui.QFontMetricsF(QtGui.QFont(obj.font()))
   except AttributeError:
      fm = QtGui.QFontMetricsF(QtGui.QFont())
   szWidth,szHeight = fm.boundingRect('abcfgijklm').width(), fm.height()
   szWidth = int(szWidth * nChar/10.0 + 0.5)
   return szWidth, int(szHeight)

#######
def tightSizeStr(obj, theStr):
   """ Measure a specific string """
   try:
      fm = QtGui.QFontMetricsF(QtGui.QFont(obj.font()))
   except AttributeError:
      fm = QtGui.QFontMetricsF(QtGui.QFont())
   szWidth,szHeight = fm.boundingRect(theStr).width(), fm.height()
   return int(szWidth), int(szHeight)

#######
def relaxedSizeStr(obj, theStr):
   """
   Approximates the size of a row text, nchars long, adds some margin
   """
   try:
      fm = QtGui.QFontMetricsF(QtGui.QFont(obj.font()))
   except AttributeError:
      fm = QtGui.QFontMetricsF(QtGui.QFont())
   szWidth,szHeight = fm.boundingRect(theStr).width(), fm.height()
   return int(10 + szWidth*1.05), int(1.5*szHeight)

#######
def relaxedSizeNChar(obj, nChar):
   """
   Approximates the size of a row text, nchars long, adds some margin
   """
   try:
      fm = QtGui.QFontMetricsF(QtGui.QFont(obj.font()))
   except AttributeError:
      fm = QtGui.QFontMetricsF(QtGui.QFont())
   szWidth,szHeight = fm.boundingRect('abcfg ijklm').width(), fm.height()
   szWidth = int(szWidth * nChar/10.0 + 0.5)
   return int(10 + szWidth*1.05), int(1.5*szHeight)

#############################################################################
def initialColResize(tblViewObj, sizeList):
   """
   We assume that all percentages are below 1, all fixed >1.
   TODO:  This seems to almost work.  providing exactly 100% input will
          actually result in somewhere between 75% and 125% (approx).
          For now, I have to experiment with initial values a few times
          before getting it to a satisfactory initial size.
   """
   totalWidth = tblViewObj.width()
   fixedCols, pctCols = [],[]

   if tblViewObj.model() is None:
      return

   for col,colVal in enumerate(sizeList):
      if colVal > 1:
         fixedCols.append( (col, colVal) )
      else:
         pctCols.append( (col, colVal) )

   for c,sz in fixedCols:
      tblViewObj.horizontalHeader().resizeSection(c, int(sz))

   totalFixed = sum([sz[1] for sz in fixedCols])
   szRemain = totalWidth-totalFixed
   for c,pct in pctCols:
      tblViewObj.horizontalHeader().resizeSection(c, int(pct*szRemain))

   tblViewObj.horizontalHeader().setStretchLastSection(True)

#############################################################################
class QRichLabel(QtWidgets.QLabel):
   def __init__(self,
         txt,
         doWrap=True,
         hAlign=QtCore.Qt.AlignLeft,
         vAlign=QtCore.Qt.AlignVCenter,
         **kwargs):
      super(QRichLabel, self).__init__(txt)
      self.setTextFormat(QtCore.Qt.RichText)
      self.setWordWrap(doWrap)
      self.setAlignment(hAlign | vAlign)
      self.setText(txt, **kwargs)
      # Fixes a problem with QtWidgets.QLabel resizing based on content
      # ACR:  ... and makes other problems.  Removing for now.
      # self.setSizePolicy(QtWidgets.QSizePolicy.Preferred,
      # QtWidgets.QSizePolicy.MinimumExpanding)
      #self.setMinimumHeight(int(relaxedSizeStr(self, 'QWERTYqypgj')[1]))

   def setText(self, text, color=None, size=None, bold=None, italic=None):
      text = str(text)
      if color:
         text = '<font color="%s">%s</font>' % (htmlColor(color), text)
      if size:
         if isinstance(size, int):
            text = '<font size=%d>%s</font>' % (size, text)
         else:
            text = '<font size="%s">%s</font>' % (size, text)
      if bold:
         text = '<b>%s</b>' % text
      if italic:
         text = '<i>%s</i>' % text

      super(QRichLabel, self).setText(text)

   def setBold(self):
      self.setText('<b>' + self.text() + '</b>')

   def setItalic(self):
      self.setText('<i>' + self.text() + '</i>')

class QRichLabel_AutoToolTip(QRichLabel):
   def __init__(self,
         txt,
         doWrap=True,
         hAlign=QtCore.Qt.AlignLeft,
         vAlign=QtCore.Qt.AlignVCenter,
         **kwargs):
      super(QRichLabel_AutoToolTip, self).__init__(
         txt, doWrap, hAlign, vAlign, **kwargs
      )

      self.toolTipMethod = None

   def setToolTipLambda(self, toolTipMethod):
      self.toolTipMethod = toolTipMethod
      self.setToolTip(self.toolTipMethod())

   def event(self, event):
      if event.type() == QtCore.QEvent.ToolTip:
         if self.toolTipMethod != None:
            txt = self.toolTipMethod()
            self.setToolTip(txt)

      return QtWidgets.QLabel.event(self,event)

class QMoneyLabel(QRichLabel):
   def __init__(self, nSatoshi, ndec=8, maxZeros=2, wColor=True,
      wBold=None, txtSize=10):
      QtWidgets.QLabel.__init__(self, coin2str(nSatoshi))

      self.nSatoshi = nSatoshi
      self.setValueText(nSatoshi, ndec, maxZeros, wColor, wBold, txtSize)

   def setValueText(self, nSatoshi, ndec=None, maxZeros=None, wColor=None,
      wBold=None, txtSize=10):
      """
      When we set the text of the QMoneyLabel, remember previous values unless
      explicitly respecified
      """
      if not ndec is None:
         self.ndec = ndec

      if not maxZeros is None:
         self.max0 = maxZeros

      if not wColor is None:
         self.colr = wColor

      if not wBold is None:
         self.bold = wBold

      theFont = GETFONT("Fixed", txtSize)
      if self.bold:
         theFont.setWeight(QtGui.QFont.Bold)

      self.setFont(theFont)
      self.setWordWrap(False)
      valStr = coin2str(nSatoshi, ndec=self.ndec, maxZeros=self.max0)
      goodMoney = htmlColor('MoneyPos')
      badMoney  = htmlColor('MoneyNeg')
      if nSatoshi < 0 and self.colr:
         self.setText('<font color=%s>%s</font>' % (badMoney, valStr))
      elif nSatoshi > 0 and self.colr:
         self.setText('<font color=%s>%s</font>' % (goodMoney, valStr))
      else:
         self.setText('%s' % valStr)
      self.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)

def setLayoutStretchRows(layout, *args):
   for i,st in enumerate(args):
      layout.setRowStretch(i, st)

def setLayoutStretchCols(layout, *args):
   for i,st in enumerate(args):
      layout.setColumnStretch(i, st)

# Use this for QtWidgets.QHBoxLayout and QtWidgets.QVBoxLayout,
# where you don't specify dimension
def setLayoutStretch(layout, *args):
   for i,st in enumerate(args):
      layout.setStretch(i, st)

################################################################################
def QPixMapButton(img):
   btn = QtWidgets.QPushButton('')
   px = QtGui.QPixmap(img)
   btn.setIcon( QtGui.QIcon(px))
   btn.setIconSize(px.rect().size())
   return btn

################################################################################
def QAcceptButton():
   return QPixMapButton('img/btnaccept.png')
def QCancelButton():
   return QPixMapButton('img/btncancel.png')
def QBackButton():
   return QPixMapButton('img/btnback.png')
def QOkButton():
   return QPixMapButton('img/btnok.png')
def QDoneButton():
   return QPixMapButton('img/btndone.png')

################################################################################
class QLabelButton(QtWidgets.QLabel):
   mousePressOn = set()

   def __init__(self, txt):
      colorStr = htmlColor('LBtnNormalFG')
      super().__init__('<font color=%s>%s</u></font>' % (colorStr, txt))
      self.plainText = txt
      self.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)

   def sizeHint(self):
      w,h = relaxedSizeStr(self, self.plainText)
      return QtCore.QSize(w, int(1.2*h))

   def mousePressEvent(self, ev):
      # Prevent click-bleed-through to dialogs being opened
      txt = toBytes(self.text())
      self.mousePressOn.add(txt)

   def mouseReleaseEvent(self, ev):
      txt = toBytes(self.text())
      if txt in self.mousePressOn:
         self.mousePressOn.remove(txt)
         self.linkActivated.emit(ev)

   def enterEvent(self, ev):
      ssStr = "QtWidgets.QLabel { background-color : %s }" % \
         htmlColor('LBtnHoverBG')
      self.setStyleSheet(ssStr)

   def leaveEvent(self, ev):
      ssStr = "QtWidgets.QLabel { background-color : %s }" % \
         htmlColor('LBtnNormalBG')
      self.setStyleSheet(ssStr)

################################################################################
def makeLayoutFrame(dirStr, widgetList, style=QtWidgets.QFrame.NoFrame,
   condenseMargins=False):
   frm = QtWidgets.QFrame()
   frm.setFrameStyle(style)

   frmLayout = QtWidgets.QHBoxLayout()
   if dirStr.lower().startswith(VERTICAL):
      frmLayout = QtWidgets.QVBoxLayout()

   for w in widgetList:
      if w is None:
         frmLayout.addWidget(w)
      elif issubclass(type(w),QtWidgets.QWidget):
         frmLayout.addWidget(w)
      elif issubclass(type(w),QtWidgets.QLayoutItem):
         frmLayout.addItem(w)
      else: #we assume at this point we are using a string/unicode
         if w.lower()=='stretch':
            frmLayout.addStretch()
         elif w.lower().startswith('line'):
            frmLine = QtWidgets.QFrame()
            if dirStr.lower().startswith(VERTICAL):
               frmLine.setFrameStyle(
                  QtWidgets.QFrame.HLine | QtWidgets.QFrame.Plain)
            else:
               frmLine.setFrameStyle(
                  QtWidgets.QFrame.VLine | QtWidgets.QFrame.Plain)
            frmLayout.addWidget(frmLine)
         elif w.lower().startswith('strut'):
            first = w.index('(')+1
            last  = w.index(')')
            strutSz = int(w[first:last])
            frmLayout.addStrut(strutSz)
         elif w.lower().startswith('space'):
            # expect "spacer(30)"
            first = w.index('(')+1
            last  = w.index(')')
            wid,hgt = int(w[first:last]), 1
            if dirStr.lower().startswith(VERTICAL):
               wid,hgt = hgt,wid
            frmLayout.addItem( QtWidgets.QSpacerItem(wid,hgt) )

   if condenseMargins:
      frmLayout.setContentsMargins(3,3,3,3)
      frmLayout.setSpacing(3)
   else:
      frmLayout.setContentsMargins(5,5,5,5)
   frm.setLayout(frmLayout)
   return frm

def addFrame(widget, style=STYLE_SUNKEN, condenseMargins=False):
   return makeLayoutFrame(HORIZONTAL, [widget], style, condenseMargins)

def makeVertFrame(widgetList, style=QtWidgets.QFrame.NoFrame, condenseMargins=False):
   return makeLayoutFrame(VERTICAL, widgetList, style, condenseMargins)

def makeHorizFrame(widgetList, style=QtWidgets.QFrame.NoFrame, condenseMargins=False):
   return makeLayoutFrame(HORIZONTAL, widgetList, style, condenseMargins)

def QImageLabel(imgfn, size=None, stretch='NoStretch'):

   lbl = QtWidgets.QLabel()

   if size==None:
      px = QtGui.QPixmap(imgfn)
   else:
      px = QtGui.QPixmap(imgfn).scaled(*size)  # expect size=(W,H)

   lbl.setPixmap(px)
   return lbl

def restoreTableView(qtbl, hexBytes):
   try:
      binunpack = BinaryUnpacker(hex_to_binary(hexBytes))
      hexByte = binunpack.get(UINT8)
      binLen = binunpack.get(UINT8)
      toRestore = []
      for i in range(binLen):
         sz = binunpack.get(UINT16)
         if sz>0:
            toRestore.append([i,sz])

      for i,c in toRestore[:-1]:
         qtbl.setColumnWidth(i, c)
   except Exception as e:
      traceback.print_tb(e.__traceback__)
      # Don't want to crash the program just because couldn't load tbl data

def saveTableView(qtbl):
   if qtbl.model() is None:
      return

   nCol = qtbl.model().columnCount()
   sz = [None]*nCol
   for i in range(nCol):
      sz[i] = qtbl.columnWidth(i)

   # Use 'ff' as a kind of magic byte for this data.  Most importantly
   # we want to guarantee that the settings file will interpret this
   # as hex data -- I once had an unlucky hex string written out with
   # all digits and then intepretted as an integer on the next load :(
   first = int_to_hex(nCol)
   rest  = [int_to_hex(s, widthBytes=2) for s in sz]
   return 'ff' + first + ''.join(rest)

################################################################################
class QRadioButtonBackupCtr(QtWidgets.QRadioButton):
   def __init__(self, parent, txt, index):
      super(QRadioButtonBackupCtr, self).__init__(txt)
      self.parent = parent
      self.index = index

   def enterEvent(self, ev):
      pass
      # self.parent.setDispFrame(self.index)
      # self.setStyleSheet('QtWidgets.QRadioButton { background-color : %s }' % \
                                          # htmlColor('SlightBkgdDark'))

   def leaveEvent(self, ev):
      pass
      # self.parent.setDispFrame(-1)
      # self.setStyleSheet('QtWidgets.QRadioButton { background-color : %s }' % \
                                          # htmlColor('Background'))

################################################################################
# This class is intended to be an abstract frame class that
# will hold all of the functionality that is common to all
# Frames used in Armory.
# The Frames that extend this class should contain all of the
# display and control components for some screen used in Armory
# Putting this content in a frame allows it to be used on it's own
# in a dialog or as a component in a larger frame.
class ArmoryFrame(QtWidgets.QFrame):
   def __init__(self, parent, main):
      super(ArmoryFrame, self).__init__(parent)
      self.main = main

      # Subclasses should implement a method that returns a boolean to control
      # when done, accept, next, or final button should be enabled.
      self.isComplete = None

# Pure-python BMP creator taken from:
#
#     http://pseentertainmentcorp.com/smf/index.php?topic=2034.0
#
# This will take a 2D array of ones-and-zeros and convert it to a binary
# bitmap image, which will be stored in a temporary file.  This temporary
# file can be used for display and copy-and-paste into email.

def bmp_binary(header, pixels):
   '''It takes a header (based on default_bmp_header),
   the pixel data (from structs, as produced by get_color and row_padding),
   and writes it to filename'''
   header_str = ""
   header_str += struct.pack('<B', header['mn1'])
   header_str += struct.pack('<B', header['mn2'])
   header_str += struct.pack('<L', header['filesize'])
   header_str += struct.pack('<H', header['undef1'])
   header_str += struct.pack('<H', header['undef2'])
   header_str += struct.pack('<L', header['offset'])
   header_str += struct.pack('<L', header['headerlength'])
   header_str += struct.pack('<L', header['width'])
   header_str += struct.pack('<L', header['height'])
   header_str += struct.pack('<H', header['colorplanes'])
   header_str += struct.pack('<H', header['colordepth'])
   header_str += struct.pack('<L', header['compression'])
   header_str += struct.pack('<L', header['imagesize'])
   header_str += struct.pack('<L', header['res_hor'])
   header_str += struct.pack('<L', header['res_vert'])
   header_str += struct.pack('<L', header['palette'])
   header_str += struct.pack('<L', header['importantcolors'])
   return header_str + pixels

def bmp_write(header, pixels, filename):
   out = open(filename, 'wb')
   out.write(bmp_binary(header, pixels))
   out.close()

def bmp_row_padding(width, colordepth):
   '''returns any necessary row padding'''
   byte_length = width*colordepth/8
   # how many bytes are needed to make byte_length evenly divisible by 4?
   padding = (4-byte_length)%4
   padbytes = ''
   for i in range(padding):
      x = struct.pack('<B',0)
      padbytes += x
   return padbytes

def bmp_pack_color(red, green, blue):
   '''accepts values from 0-255 for each value, returns a packed string'''
   return struct.pack('<BBB',blue,green,red)

###################################
BMP_TEMPFILE = -1
def createBitmap(imgMtrx2D, writeToFile=-1, returnBinary=True):
   try:
      h,w = len(imgMtrx2D), len(imgMtrx2D[0])
   except:
      LOGERROR('Error creating BMP object')
      raise

   header = {'mn1':66,
             'mn2':77,
             'filesize':0,
             'undef1':0,
             'undef2':0,
             'offset':54,
             'headerlength':40,
             'width':w,
             'height':h,
             'colorplanes':0,
             'colordepth':24,
             'compression':0,
             'imagesize':0,
             'res_hor':0,
             'res_vert':0,
             'palette':0,
             'importantcolors':0}

   pixels = ''
   black = bmp_pack_color(  0,  0,  0)
   white = bmp_pack_color(255,255,255)
   for row in range(header['height']-1,-1,-1):# (BMPs are L to R from the bottom L row)
      for col in range(header['width']):
         pixels += black if imgMtrx2D[row][col] else white
      pixels += bmp_row_padding(header['width'], header['colordepth'])

   if returnBinary:
      return bmp_binary(header,pixels)
   elif writeToFile==BMP_TEMPFILE:
      handle,temppath = mkstemp(suffix='.bmp')
      bmp_write(header, pixels, temppath)
      return temppath
   else:
      try:
         bmp_write(header, pixels, writeToFile)
         return True
      except:
         return False

def selectFileForQLineEdit(parent, qObj, title="Select File", existing=False,
   ffilter=[]):
   initPath = ARMORY_HOME_DIR
   currText = str(qObj.text()).strip()
   if currText:
      if os.path.exists(currText):
         initPath = currText

   types = list(ffilter)
   types.append('All files (*)')
   typesStr = ';; '.join(types)
   if not OS_MACOSX:
      fullPath, _ = QtWidgets.QFileDialog.getOpenFileName(parent,
         title, ARMORY_HOME_DIR, typesStr)
   else:
      fullPath, _ = QtWidgets.QFileDialog.getOpenFileName(parent,
         title, ARMORY_HOME_DIR, typesStr,
         options=QtWidgets.QFileDialog.DontUseNativeDialog)

   if fullPath:
      qObj.setText(fullPath)

def selectDirectoryForQLineEdit(par, qObj, title="Select Directory"):
   initPath = ARMORY_HOME_DIR
   currText = str(qObj.text()).strip()
   if currText:
      if os.path.exists(currText):
         initPath = currText

   if not OS_MACOSX:
      fullPath = QtWidgets.QFileDialog.getExistingDirectory(par, title, initPath)
   else:
      fullPath = QtWidgets.QFileDialog.getExistingDirectory(
         par, title, initPath,
         options=QtWidgets.QFileDialog.DontUseNativeDialog
      )
   if fullPath:
      if isinstance(fullPath, list):
         fullPath = fullPath[0]
      qObj.setText(fullPath)

def createDirectorySelectButton(parent, targetWidget, title="Select Directory"):

   btn = QtWidgets.QPushButton('')
   ico = QtGui.QIcon(QtGui.QPixmap('./img/folder24.png'))
   btn.setIcon(ico)

   fn = lambda: selectDirectoryForQLineEdit(parent, targetWidget, title)
   btn.clicked.connect(fn)
   return btn

#############################################################################
class LetterButton(QtWidgets.QPushButton):
   def __init__(self, Low, Up, Row, Spec, edtTarget, parent):
      super(LetterButton, self).__init__('')
      self.lower = Low
      self.upper = Up
      self.defRow = Row
      self.special = Spec
      self.target = edtTarget
      self.parent = parent

      if self.special:
         super(LetterButton, self).setFont(GETFONT('Var', 8))
      else:
         super(LetterButton, self).setFont(GETFONT('Fixed', 10))
      if self.special == 'space':
         self.setText(self.tr('SPACE'))
         self.lower = ' '
         self.upper = ' '
         self.special = 5
      elif self.special == 'shift':
         self.setText(self.tr('SHIFT'))
         self.special = 5
         self.insertLetter = self.pressShift
      elif self.special == 'delete':
         self.setText(self.tr('DEL'))
         self.special = 5
         self.insertLetter = self.pressBackspace

   def insertLetter(self):
      currPwd = str(self.parent.edtPasswd.text())
      insChar = self.upper if self.parent.btnShift.isChecked() else self.lower
      if len(insChar) == 2 and insChar.startswith('#'):
         insChar = insChar[1]

      self.parent.edtPasswd.setText(currPwd + insChar)
      self.parent.reshuffleKeys()

   def pressShift(self):
      self.parent.redrawKeys()

   def pressBackspace(self):
      currPwd = str(self.parent.edtPasswd.text())
      if len(currPwd) > 0:
         self.parent.edtPasswd.setText(currPwd[:-1])
      self.parent.redrawKeys()

#############################################################################
def createToolTipWidget(tiptext, iconSz=2):
   """
   The <u></u> is to signal to QtCore.Qt that it should be interpretted as HTML/Rich
   text even if no HTML tags are used.  This appears to be necessary for QtCore.Qt
   to wrap the tooltip text
   """
   fgColor = htmlColor('ToolTipQ')
   lbl = QtWidgets.QLabel('<font size=%d color=%s>(?)</font>' % (iconSz, fgColor))
   lbl.setMaximumWidth(int(relaxedSizeStr(lbl, '(?)')[0]))

   def setAllText(widget, txt):
      def pressEv(ev):
         QtWidgets.QWhatsThis.showText(ev.globalPos(), txt, widget)
      widget.mousePressEvent = pressEv
      widget.setToolTip('<u></u>' + txt)

   # Calling setText on this widget will update both the tooltip and QWT
   from types import MethodType
   lbl.setText = MethodType(setAllText, lbl)
   lbl.setText(tiptext)
   return lbl

#############################################################################
class AdvancedOptionsFrame(ArmoryFrame):
   def __init__(self, parent, main, initLabel=''):
      super(AdvancedOptionsFrame, self).__init__(parent, main)
      lblComputeDescription = QRichLabel( \
                  self.tr('Armory will test your system\'s speed to determine the most '
                  'challenging encryption settings that can be applied '
                  'in a given amount of time.  High settings make it much harder '
                  'for someone to guess your passphrase.  This is used for all '
                  'encrypted wallets, but the default parameters can be changed below.\n'))
      lblComputeDescription.setWordWrap(True)
      timeDescriptionTip = createToolTipWidget( \
                  self.tr('This is the amount of time it will take for your computer '
                  'to unlock your wallet after you enter your passphrase. '
                  '(the actual time used will be less than the specified '
                  'time, but more than one half of it).  '))

      # Set maximum compute time
      self.editComputeTime = QtWidgets.QLineEdit()
      self.editComputeTime.setText('250 ms')
      self.editComputeTime.setMaxLength(12)
      lblComputeTime = QtWidgets.QLabel(self.tr('Target compute &time (s, ms):'))
      memDescriptionTip = createToolTipWidget( \
                  self.tr('This is the <b>maximum</b> memory that will be '
                  'used as part of the encryption process.  The actual value used '
                  'may be lower, depending on your system\'s speed.  If a '
                  'low value is chosen, Armory will compensate by chaining '
                  'together more calculations to meet the target time.  High '
                  'memory target will make GPU-acceleration useless for '
                  'guessing your passphrase.'))
      lblComputeTime.setBuddy(self.editComputeTime)

      # Set maximum memory usage
      self.editComputeMem = QtWidgets.QLineEdit()
      self.editComputeMem.setText('32.0 MB')
      self.editComputeMem.setMaxLength(12)
      lblComputeMem  = QtWidgets.QLabel(self.tr('Max &memory usage (kB, MB):'))
      lblComputeMem.setBuddy(self.editComputeMem)

      self.editComputeTime.setMaximumWidth( tightSizeNChar(self, 20)[0] )
      self.editComputeMem.setMaximumWidth( tightSizeNChar(self, 20)[0] )

      entryFrame = QtWidgets.QFrame()
      entryLayout = QtWidgets.QGridLayout()
      entryLayout.addWidget(timeDescriptionTip,        0, 0,  1, 1)
      entryLayout.addWidget(lblComputeTime,      0, 1,  1, 1)
      entryLayout.addWidget(self.editComputeTime, 0, 2,  1, 1)
      entryLayout.addWidget(memDescriptionTip,         1, 0,  1, 1)
      entryLayout.addWidget(lblComputeMem,       1, 1,  1, 1)
      entryLayout.addWidget(self.editComputeMem,  1, 2,  1, 1)
      entryFrame.setLayout(entryLayout)
      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblComputeDescription)
      layout.addWidget(entryFrame)
      layout.addStretch()
      self.setLayout(layout)

   def getKdfSec(self):
      # return -1 if the input is invalid
      kdfSec = -1
      try:
         kdfT, kdfUnit = str(self.editComputeTime.text()).strip().split(' ')
         if kdfUnit.lower() == 'ms':
            kdfSec = float(kdfT) / 1000.
         elif kdfUnit.lower() in ('s', 'sec', 'seconds'):
            kdfSec = float(kdfT)
      except:
         pass
      return kdfSec

   def getKdfBytes(self):
      # return -1 if the input is invalid
      kdfBytes = -1
      try:
         kdfM, kdfUnit = str(self.editComputeMem.text()).split(' ')
         if kdfUnit.lower() == 'mb':
            kdfBytes = round(float(kdfM) * (1024.0 ** 2))
         elif kdfUnit.lower() == 'kb':
            kdfBytes = round(float(kdfM) * (1024.0))
      except:
         pass
      return kdfBytes
################################################################################
# Generic cell helpers for tree/list widgets
################################################################################
def makeCenteredCell(childWidget):
   """Wrap a widget in a zero-margin, center-aligned container for cells."""
   wrapper = QtWidgets.QWidget()
   layout = QtWidgets.QHBoxLayout(wrapper)
   layout.setContentsMargins(0, 0, 0, 0)
   layout.setAlignment(QtCore.Qt.AlignCenter)
   layout.addWidget(childWidget)
   return wrapper

def makeCheckboxCell(enabled, checked, onToggled=None):
   """Create a centered checkbox cell; returns (cellWidget, checkbox)."""
   cb = QtWidgets.QCheckBox()
   cb.setEnabled(bool(enabled))
   cb.setChecked(bool(checked))
   if onToggled is not None:
      cb.stateChanged.connect(onToggled)
   return makeCenteredCell(cb), cb

def addPlaceholderRow(treeWidget, texts):
   """Add a placeholder top-level row to a QTreeWidget with given column texts."""
   item = QtWidgets.QTreeWidgetItem()
   for idx, txt in enumerate(texts):
      item.setText(idx, txt)
   treeWidget.addTopLevelItem(item)
   return item

################################################################################
# Setup dialog helpers
################################################################################
def createTabTitle(text):
   """Create a centered, bold title label for tab headers."""
   title = QRichLabel(f'<span style="font-size:14pt;"><b>{text}</b></span>')
   title.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)
   return title

def createDirectoryInputLayout(parent, lineEdit, browseTitle="Select Directory"):
   """
   Create a directory input layout with browse button.
   Returns (layout, browseButton) tuple.
   """
   browseBtn = QtWidgets.QPushButton(parent.tr("Browse..."))
   browseBtn.setFixedWidth(100)
   browseBtn.clicked.connect(
      lambda: selectDirectoryForQLineEdit(parent, lineEdit, browseTitle))
   layout = QtWidgets.QHBoxLayout()
   layout.setSpacing(8)
   layout.addWidget(lineEdit)
   layout.addWidget(browseBtn)
   return layout, browseBtn
