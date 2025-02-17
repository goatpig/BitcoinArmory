# -*- coding: UTF-8 -*-
from __future__ import (absolute_import, division,
                        print_function, unicode_literals)

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

import functools
import shutil
import socket
import sys
import time
from zipfile import ZipFile, ZIP_DEFLATED

from qtpy import QtCore, QtGui, QtWidgets

from armorycolors import Colors, htmlColor
from armoryengine.MultiSigUtils import calcLockboxID, createLockboxEntryStr,\
   LBPREFIX, isBareLockbox, isP2SHLockbox
from armoryengine.ArmoryUtils import BTC_HOME_DIR, MAX_COMMENT_LENGTH, \
   UNKNOWN

from ui.TreeViewGUI import AddressTreeModel
from ui.QrCodeMatrix import CreateQRMatrix
from armoryengine.Block import PyBlockHeader

from qtdialogs.qtdefines import USERMODE, GETFONT, tightSizeStr, \
   MSGBOX, QRichLabel

from qtdialogs.ArmoryDialog import ArmoryDialog
from qtdialogs.MsgBoxCustom import MsgBoxCustom

NO_CHANGE = 'NoChange'
BACKUP_TYPE_135A = '1.35a'
BACKUP_TYPE_135C = '1.35c'
BACKUP_TYPE_0_TEXT = 'Version 0  (from script, 9 lines)'
BACKUP_TYPE_135a_TEXT = 'Version 1.35a (5 lines Unencrypted)'
BACKUP_TYPE_135a_SP_TEXT = u'Version 1.35a (5 lines + SecurePrint\u200b\u2122)'
BACKUP_TYPE_135c_TEXT = 'Version 1.35c (3 lines Unencrypted)'
BACKUP_TYPE_135c_SP_TEXT = u'Version 1.35c (3 lines + SecurePrint\u200b\u2122)'
MAX_QR_SIZE = 198
MAX_SATOSHIS = 2100000000000000

################################################################################
class DlgGenericGetPassword(ArmoryDialog):
   def __init__(self, descriptionStr, parent=None, main=None):
      super(DlgGenericGetPassword, self).__init__(parent, main)


      lblDescr = QRichLabel(descriptionStr)
      lblPasswd = QRichLabel(self.tr("Password:"))
      self.edtPasswd = QtWidgets.QLineEdit()
      self.edtPasswd.setEchoMode(QtWidgets.QLineEdit.Password)
      self.edtPasswd.setMinimumWidth(MIN_PASSWD_WIDTH(self))
      self.edtPasswd.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Expanding)

      self.btnAccept = QtWidgets.QPushButton(self.tr("OK"))
      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.connect(self.btnAccept, SIGNAL(CLICKED), self.accept)
      self.connect(self.btnCancel, SIGNAL(CLICKED), self.reject)
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)

      layout = QtWidgets.QGridLayout()
      layout.addWidget(lblDescr, 1, 0, 1, 2)
      layout.addWidget(lblPasswd, 2, 0, 1, 1)
      layout.addWidget(self.edtPasswd, 2, 1, 1, 1)
      layout.addWidget(buttonBox, 3, 1, 1, 2)

      self.setLayout(layout)
      self.setWindowTitle(self.tr('Enter Password'))
      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))

################################################################################
# Hack!  We need to replicate the DlgBugReport... but to be as safe as
# possible for 0.91.1, we simply duplicate the dialog and modify directly.
# TODO:  There's definitely a way to make DlgBugReport more generic so that
#        both these contexts can be handled by it.
class DlgInconsistentWltReport(ArmoryDialog):

   def __init__(self, parent, main, logPathList):
      super(DlgInconsistentWltReport, self).__init__(parent, main)


      QtWidgets.QMessageBox.critical(self, self.tr('Inconsistent Wallet!'), self.tr(
         '<font color="%s" size=4><b><u>Important:</u>  Wallet Consistency'
         'Issues Detected!</b></font>'
         '<br><br>'
         'Armory now detects certain kinds of hardware errors, and one'
         'or more of your wallets'
         'was flagged.  The consistency logs need to be analyzed by the'
         'Armory team to determine if any further action is required.'
         '<br><br>'
         '<b>This warning will pop up every time you start Armory until'
         'the wallet is fixed</b>' % htmlColor('TextWarn')),
         QtWidgets.QMessageBox.Ok)



      # logPathList is [wltID, corruptFolder] pairs
      self.logPathList = logPathList[:]
      walletList = [self.main.walletMap[wid] for wid,folder in logPathList]

      getWltStr = lambda w: '<b>Wallet "%s" (%s)</b>' % \
                                       (w.labelName, w.uniqueIDB58)

      if len(logPathList) == 1:
         wltDispStr = getWltStr(walletList[0]) + ' is'
      else:
         strList = [getWltStr(w) for w in walletList]
         wltDispStr = ', '.join(strList[:-1]) + ' and ' + strList[-1] + ' are '

      lblTopDescr = QRichLabel(self.tr(
         '<b><u><font color="%s" size=4>Submit Wallet Analysis Logs for '
         'Review</font></u></b><br>' % htmlColor('TextWarn')),
         hAlign=QtCore.Qt.AlignHCenter)

      lblDescr = QRichLabel(self.tr(
         'Armory has detected that %s is inconsistent, '
         'possibly due to hardware errors out of our control.  It <u>strongly '
         'recommended</u> you submit the wallet logs to the Armory developers '
         'for review.  Until you hear back from an Armory developer, '
         'it is recommended that you: '
         '<ul>'
         '<li><b>Do not delete any data in your Armory home directory</b></li> '
         '<li><b>Do not send or receive any funds with the affected wallet(s)</b></li> '
         '<li><b>Create a backup of the wallet analysis logs</b></li> '
         '</ul>' % wltDispStr))

      btnBackupLogs = QtWidgets.QPushButton(self.tr("Save backup of log files"))
      self.connect(btnBackupLogs, SIGNAL('clicked()'), self.doBackupLogs)
      frmBackup = makeHorizFrame(['Stretch', btnBackupLogs, 'Stretch'])

      self.lblSubject = QRichLabel(self.tr('Subject:'))
      self.edtSubject = QtWidgets.QLineEdit()
      self.edtSubject.setMaxLength(64)
      self.edtSubject.setText("Wallet Consistency Logs")

      self.txtDescr = QtWidgets.QTextEdit()
      self.txtDescr.setFont(GETFONT('Fixed', 9))
      w,h = tightSizeNChar(self, 80)
      self.txtDescr.setMinimumWidth(w)
      self.txtDescr.setMinimumHeight(int(2.5*h))

      self.btnCancel = QtWidgets.QPushButton(self.tr('Close'))
      self.btnbox = QtWidgets.QDialogButtonBox()
      self.btnbox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)
      self.connect(self.btnCancel, SIGNAL(CLICKED), self, SLOT('reject()'))

      layout = QtWidgets.QGridLayout()
      i = -1

      i += 1
      layout.addWidget(lblTopDescr,      i,0, 1,2)

      i += 1
      layout.addWidget(lblDescr,         i,0, 1,2)

      i += 1
      layout.addWidget(frmBackup,        i,0, 1,2)

      i += 1
      layout.addWidget(HLINE(),          i,0, 1,2)

      i += 1
      layout.addWidget(self.btnbox,      i,0, 1,2)

      self.setLayout(layout)
      self.setWindowTitle(self.tr('Inconsistent Wallet'))
      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))

   #############################################################################
   def createZipfile(self, zfilePath=None, forceIncludeAllData=False):
      """
      If not forceIncludeAllData, then we will exclude wallet file and/or
      regular logs, depending on the user's checkbox selection.   For making
      a user backup, we always want to include everything, regardless of
      that selection.
      """

      # Should we include wallet files from logs directory?
      includeWlt = self.chkIncludeWOW.isChecked()
      includeReg = self.chkIncludeReg.isChecked()

      # Set to default save path if needed
      if zfilePath is None:
         zfilePath = os.path.join(ARMORY_HOME_DIR, 'wallet_analyze_logs.zip')

      # Remove a previous copy
      if os.path.exists(zfilePath):
         os.remove(zfilePath)

      LOGINFO('Creating archive: %s', zfilePath)
      zfile = ZipFile(zfilePath, 'w', ZIP_DEFLATED)

      # Iterate over all log directories (usually one)
      for wltID,logDir in self.logPathList:
         for fn in os.listdir(logDir):
            fullpath = os.path.join(logDir, fn)

            # If multiple dirs, will see duplicate armorylogs and multipliers
            if not os.path.isfile(fullpath):
               continue


            if not forceIncludeAllData:
               # Exclude any wallet files if the checkbox was not checked
               if not includeWlt and os.path.getsize(fullpath) >= 8:
                  # Don't exclude based on file extension, check leading bytes
                  with open(fullpath, 'rb') as tempopen:
                     if tempopen.read(8) == '\xbaWALLET\x00':
                        continue

               # Exclude regular logs as well, if desired
               if not includeReg and fn in ['armorylog.txt', 'armorycpplog.txt', 'dbLog.txt']:
                  continue


            # If we got here, add file to archive
            parentDir = os.path.basename(logDir)
            archiveName = '%s_%s_%s' % (wltID, parentDir, fn)
            LOGINFO('   Adding %s to archive' % archiveName)
            zfile.write(fullpath, archiveName)

      zfile.close()

      return zfilePath


   #############################################################################
   def doBackupLogs(self):
      saveTo = self.main.getFileSave(ffilter=['Zip files (*.zip)'],
                                     defaultFilename='wallet_analyze_logs.zip')
      if not saveTo:
         QtWidgets.QMessageBox.critical(self, self.tr("Not saved"), self.tr(
            'You canceled the backup operation.  No backup was made.'),
            QtWidgets.QMessageBox.Ok)
         return

      try:
         self.createZipfile(saveTo, forceIncludeAllData=True)
         QtWidgets.QMessageBox.information(self, self.tr('Success'), self.tr(
            'The wallet logs were successfully saved to the following'
            'location:'
            '<br><br>'
            '%s'
            '<br><br>'
            'It is still important to complete the rest of this form'
            'and submit the data to the Armory team for review!' % saveTo), QtWidgets.QMessageBox.Ok)

      except:
         LOGEXCEPT('Failed to create zip file')
         QtWidgets.QMessageBox.warning(self, self.tr('Save Failed'), self.tr('There was an '
            'error saving a copy of your log files'), QtWidgets.QMessageBox.Ok)




################################################################################
class DlgNewWallet(ArmoryDialog):

   def __init__(self, parent=None, main=None, initLabel=''):
      super(DlgNewWallet, self).__init__(parent, main)


      self.selectedImport = False

      # Options for creating a new wallet
      lblDlgDescr = QRichLabel(self.tr(
         'Create a new wallet for managing your funds.<br> '
         'The name and description can be changed at any time.'))
      lblDlgDescr.setWordWrap(True)

      self.edtName = QtWidgets.QLineEdit()
      self.edtName.setMaxLength(32)
      self.edtName.setText(initLabel)
      lblName = QtWidgets.QLabel("Wallet &name:")
      lblName.setBuddy(self.edtName)


      self.edtDescr = QtWidgets.QTextEdit()
      self.edtDescr.setMaximumHeight(75)
      lblDescr = QtWidgets.QLabel("Wallet &description:")
      lblDescr.setAlignment(QtCore.Qt.AlignVCenter)
      lblDescr.setBuddy(self.edtDescr)

      buttonBox = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | \
                                   QtWidgets.QDialogButtonBox.Cancel)



      # Advanced Encryption Options
      lblComputeDescr = QtWidgets.QLabel(self.tr(
                  'Armory will test your system\'s speed to determine the most '
                  'challenging encryption settings that can be performed '
                  'in a given amount of time.  High settings make it much harder '
                  'for someone to guess your passphrase.  This is used for all '
                  'encrypted wallets, but the default parameters can be changed below.\n'))
      lblComputeDescr.setWordWrap(True)
      timeDescrTip = self.main.createToolTipWidget(self.tr(
                  'This is the amount of time it will take for your computer '
                  'to unlock your wallet after you enter your passphrase. '
                  '(the actual time used will be less than the specified '
                  'time, but more than one half of it).'))


      # Set maximum compute time
      self.edtComputeTime = QtWidgets.QLineEdit()
      self.edtComputeTime.setText('250 ms')
      self.edtComputeTime.setMaxLength(12)
      lblComputeTime = QtWidgets.QLabel('Target compute &time (s, ms):')
      memDescrTip = self.main.createToolTipWidget(self.tr(
                  'This is the <b>maximum</b> memory that will be '
                  'used as part of the encryption process.  The actual value used '
                  'may be lower, depending on your system\'s speed.  If a '
                  'low value is chosen, Armory will compensate by chaining '
                  'together more calculations to meet the target time.  High '
                  'memory target will make GPU-acceleration useless for '
                  'guessing your passphrase.'))
      lblComputeTime.setBuddy(self.edtComputeTime)


      # Set maximum memory usage
      self.edtComputeMem = QtWidgets.QLineEdit()
      self.edtComputeMem.setText('32.0 MB')
      self.edtComputeMem.setMaxLength(12)
      lblComputeMem = QtWidgets.QLabel(self.tr('Max &memory usage (kB, MB):'))
      lblComputeMem.setBuddy(self.edtComputeMem)

      self.edtComputeTime.setMaximumWidth(tightSizeNChar(self, 20)[0])
      self.edtComputeMem.setMaximumWidth(tightSizeNChar(self, 20)[0])

      # Fork watching-only wallet
      cryptoLayout = QtWidgets.QGridLayout()
      cryptoLayout.addWidget(lblComputeDescr, 0, 0, 1, 3)

      cryptoLayout.addWidget(timeDescrTip, 1, 0, 1, 1)
      cryptoLayout.addWidget(lblComputeTime, 1, 1, 1, 1)
      cryptoLayout.addWidget(self.edtComputeTime, 1, 2, 1, 1)

      cryptoLayout.addWidget(memDescrTip, 2, 0, 1, 1)
      cryptoLayout.addWidget(lblComputeMem, 2, 1, 1, 1)
      cryptoLayout.addWidget(self.edtComputeMem, 2, 2, 1, 1)

      self.cryptoFrame = QtWidgets.QFrame()
      self.cryptoFrame.setFrameStyle(STYLE_SUNKEN)
      self.cryptoFrame.setLayout(cryptoLayout)
      self.cryptoFrame.setVisible(False)

      self.chkUseCrypto = QtWidgets.QCheckBox(self.tr("Use wallet &encryption"))
      self.chkUseCrypto.setChecked(True)
      usecryptoTooltip = self.main.createToolTipWidget(self.tr(
                  'Encryption prevents anyone who accesses your computer '
                  'or wallet file from being able to spend your money, as '
                  'long as they do not have the passphrase. '
                  'You can choose to encrypt your wallet at a later time '
                  'through the wallet properties dialog by double clicking '
                  'the wallet on the dashboard.'))

      # For a new wallet, the user may want to print out a paper backup
      self.chkPrintPaper = QtWidgets.QCheckBox(self.tr("Print a paper-backup of this wallet"))
      self.chkPrintPaper.setChecked(True)
      paperBackupTooltip = self.main.createToolTipWidget(self.tr(
                  'A paper-backup allows you to recover your wallet/funds even '
                  'if you lose your original wallet file, any time in the future. '
                  'Because Armory uses "deterministic wallets," '
                  'a single backup when the wallet is first made is sufficient '
                  'for all future transactions (except ones to imported '
                  'addresses).\n\n'
                  'Anyone who gets hold of your paper backup will be able to spend '
                  'the money in your wallet, so please secure it appropriately.'))


      self.btnAccept = QtWidgets.QPushButton(self.tr("Accept"))
      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.btnAdvCrypto = QtWidgets.QPushButton(self.tr("Advanced Encryption Options>>>"))
      self.btnAdvCrypto.setCheckable(True)
      self.btnbox = QtWidgets.QDialogButtonBox()
      self.btnbox.addButton(self.btnAdvCrypto, QtWidgets.QDialogButtonBox.ActionRole)
      self.btnbox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)
      self.btnbox.addButton(self.btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)

      self.connect(self.btnAdvCrypto, SIGNAL('toggled(bool)'), \
                   self.cryptoFrame, SLOT('setVisible(bool)'))
      self.connect(self.btnAccept, SIGNAL(CLICKED), \
                   self.verifyInputsBeforeAccept)
      self.connect(self.btnCancel, SIGNAL(CLICKED), \
                   self, SLOT('reject()'))


      self.btnImportWlt = QtWidgets.QPushButton(self.tr("Import wallet..."))
      self.connect(self.btnImportWlt, SIGNAL("clicked()"), \
                    self.importButtonClicked)

      masterLayout = QtWidgets.QGridLayout()
      masterLayout.addWidget(lblDlgDescr, 1, 0, 1, 2)
      # masterLayout.addWidget(self.btnImportWlt,  1, 2, 1, 1)
      masterLayout.addWidget(lblName, 2, 0, 1, 1)
      masterLayout.addWidget(self.edtName, 2, 1, 1, 2)
      masterLayout.addWidget(lblDescr, 3, 0, 1, 2)
      masterLayout.addWidget(self.edtDescr, 3, 1, 2, 2)
      masterLayout.addWidget(self.chkUseCrypto, 5, 0, 1, 1)
      masterLayout.addWidget(usecryptoTooltip, 5, 1, 1, 1)
      masterLayout.addWidget(self.chkPrintPaper, 6, 0, 1, 1)
      masterLayout.addWidget(paperBackupTooltip, 6, 1, 1, 1)
      masterLayout.addWidget(self.cryptoFrame, 8, 0, 3, 3)

      masterLayout.addWidget(self.btnbox, 11, 0, 1, 2)

      masterLayout.setVerticalSpacing(5)

      self.setLayout(masterLayout)

      self.layout().setSizeConstraint(QtWidgets.QLayout.SetFixedSize)

      self.connect(self.chkUseCrypto, SIGNAL("clicked()"), \
                   self.cryptoFrame, SLOT("setEnabled(bool)"))

      self.setWindowTitle(self.tr('Create Armory wallet'))
      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))



   def importButtonClicked(self):
      self.selectedImport = True
      self.accept()

   def verifyInputsBeforeAccept(self):

      ### Confirm that the name and descr are within size limits #######
      wltName = self.edtName.text()
      wltDescr = self.edtDescr.toPlainText()
      if len(wltName) < 1:
         QtWidgets.QMessageBox.warning(self, self.tr('Invalid wallet name'), \
                  self.tr('You must enter a name for this wallet, up to 32 characters.'), \
                  QtWidgets.QMessageBox.Ok)
         return False

      if len(wltDescr) > 256:
         reply = QtWidgets.QMessageBox.warning(self, self.tr('Input too long'), self.tr(
                  'The wallet description is limited to 256 characters.  Only the first '
                  '256 characters will be used.'), \
                  QtWidgets.QMessageBox.Ok | QtWidgets.QMessageBox.Cancel)
         if reply == QtWidgets.QMessageBox.Ok:
            self.edtDescr.setText(wltDescr[:256])
         else:
            return False

      ### Check that the KDF inputs are well-formed ####################
      try:
         kdfT, kdfUnit = str(self.edtComputeTime.text()).strip().split(' ')
         if kdfUnit.lower() == 'ms':
            self.kdfSec = float(kdfT) / 1000.
         elif kdfUnit.lower() in ('s', 'sec', 'seconds'):
            self.kdfSec = float(kdfT)

         if not (self.kdfSec <= 20.0):
            QtWidgets.QMessageBox.critical(self, self.tr('Invalid KDF Parameters'), self.tr(
               'Please specify a compute time no more than 20 seconds. '
               'Values above one second are usually unnecessary.'))
            return False

         kdfM, kdfUnit = str(self.edtComputeMem.text()).split(' ')
         if kdfUnit.lower() == 'mb':
            self.kdfBytes = round(float(kdfM) * (1024.0 ** 2))
         if kdfUnit.lower() == 'kb':
            self.kdfBytes = round(float(kdfM) * (1024.0))

         if not (2 ** 15 <= self.kdfBytes <= 2 ** 31):
            QtWidgets.QMessageBox.critical(self, self.tr('Invalid KDF Parameters'), \
               self.tr('Please specify a maximum memory usage between 32 kB and 2048 MB.'))
            return False

         LOGINFO('KDF takes %0.2f seconds and %d bytes', self.kdfSec, self.kdfBytes)
      except:
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid Input'), self.tr(
            'Please specify time with units, such as '
            '"250 ms" or "2.1 s".  Specify memory as kB or MB, such as '
            '"32 MB" or "256 kB". '), QtWidgets.QMessageBox.Ok)
         return False


      self.accept()


   def getImportWltPath(self):
      self.importFile = QtWidgets.QFileDialog.getOpenFileName(self, self.tr('Import Wallet File'), \
          ARMORY_HOME_DIR, self.tr('Wallet files (*.wallet);; All files (*)'))
      if self.importFile:
         self.accept()

################################################################################
class LoadingDisp(ArmoryDialog):
   def __init__(self, parent, main):
      super(LoadingDisp, self).__init__(parent, main)
      layout = QtWidgets.QGridLayout()
      self.setLayout(layout)
      self.barLoading = QtWidgets.QProgressBar(self)
      self.barLoading.setRange(0,100)
      self.barLoading.setFormat('%p%')
      self.barLoading.setValue(0)
      layout.addWidget(self.barLoading, 0, 0, 1, 1)
      self.setWindowTitle('Loading...')
      self.setFocus()

   def setValue(self, val):
      self.barLoading.setValue(val)

#############################################################################
class DlgImportAddress(ArmoryDialog):
   def __init__(self, wlt, parent=None, main=None):
      super(DlgImportAddress, self).__init__(parent, main)

      self.wlt = wlt


      lblImportLbl = QRichLabel(self.tr('Enter:'))

      self.radioImportOne = QtWidgets.QRadioButton(self.tr('One Key'))
      self.radioImportMany = QtWidgets.QRadioButton(self.tr('Multiple Keys'))
      btngrp = QtWidgets.QButtonGroup(self)
      btngrp.addButton(self.radioImportOne)
      btngrp.addButton(self.radioImportMany)
      btngrp.setExclusive(True)
      self.radioImportOne.setChecked(True)
      self.connect(self.radioImportOne, SIGNAL(CLICKED), self.clickImportCount)
      self.connect(self.radioImportMany, SIGNAL(CLICKED), self.clickImportCount)

      frmTop = makeHorizFrame([lblImportLbl, self.radioImportOne, \
                                             self.radioImportMany, STRETCH])
      self.stackedImport = QStackedWidget()

      # Set up the single-key import widget
      lblDescrOne = QRichLabel(self.tr('The key can either be imported into your wallet, '
                     'or have its available balance "swept" to another address '
                     'in your wallet.  Only import private '
                     'key data if you are absolutely sure that no one else '
                     'has access to it.  Otherwise, sweep it to get '
                     'the funds out of it.  All standard private-key formats '
                     'are supported <i>except for private keys created by '
                     'Bitcoin Core version 0.6.0 and later (compressed)</i>.'))

      lblPrivOne = QRichLabel('Private Key')
      self.edtPrivData = QtWidgets.QLineEdit()
      self.edtPrivData.setMinimumWidth(tightSizeStr(self.edtPrivData, 'X' * 60)[0])
      privTooltip = self.main.createToolTipWidget(self.tr(
                       'Supported formats are any hexadecimal or Base58 '
                       'representation of a 32-byte private key (with or '
                       'without checksums), and mini-private-key format '
                       'used on Casascius physical bitcoins.  Private keys '
                       'that use <i>compressed</i> public keys are not yet '
                       'supported by Armory.'))

      frmMid1 = makeHorizFrame([lblPrivOne, self.edtPrivData, privTooltip])
      stkOne = makeVertFrame([HLINE(), lblDescrOne, frmMid1, STRETCH])
      self.stackedImport.addWidget(stkOne)



      # Set up the multi-Sig import widget
      lblDescrMany = QRichLabel(self.tr(
                   'Enter a list of private keys to be "swept" or imported. '
                   'All standard private-key formats are supported.'))
      lblPrivMany = QRichLabel('Private Key List')
      lblPrivMany.setAlignment(QtCore.Qt.AlignTop)
      ttipPrivMany = self.main.createToolTipWidget(self.tr(
                  'One private key per line, in any standard format. '
                  'Data may be copied directly from the "Export Key Lists" '
                  'dialog (all text on a line preceding '
                  'the key data, separated by a colon, will be ignored).'))
      self.txtPrivBulk = QtWidgets.QTextEdit()
      w, h = tightSizeStr(self.edtPrivData, 'X' * 70)
      self.txtPrivBulk.setMinimumWidth(w)
      self.txtPrivBulk.setMinimumHeight(2.2 * h)
      self.txtPrivBulk.setMaximumHeight(4.2 * h)
      frmMid = makeHorizFrame([lblPrivMany, self.txtPrivBulk, ttipPrivMany])
      stkMany = makeVertFrame([HLINE(), lblDescrMany, frmMid])
      self.stackedImport.addWidget(stkMany)


      self.chkUseSP = QtWidgets.QCheckBox(self.tr('This is from a backup with SecurePrint\x99'))
      self.edtSecurePrint = QtWidgets.QLineEdit()
      self.edtSecurePrint.setFont(GETFONT('Fixed',9))
      self.edtSecurePrint.setEnabled(False)
      w, h = relaxedSizeStr(self.edtSecurePrint, 'X' * 12)
      self.edtSecurePrint.setMaximumWidth(w)

      def toggleSP():
         if self.chkUseSP.isChecked():
            self.edtSecurePrint.setEnabled(True)
         else:
            self.edtSecurePrint.setEnabled(False)

      self.chkUseSP.stateChanged.connect(toggleSP)
      frmSP = makeHorizFrame([self.chkUseSP, self.edtSecurePrint, 'Stretch'])
      #frmSP.setFrameStyle(STYLE_PLAIN)


      # Set up the Import/Sweep select frame
      # # Import option
      self.radioSweep = QtWidgets.QRadioButton(self.tr('Sweep any funds owned by these addresses '
                                      'into your wallet\n'
                                      'Select this option if someone else gave you this key'))
      self.radioImport = QtWidgets.QRadioButton(self.tr('Import these addresses to your wallet\n'
                                      'Only select this option if you are positive '
                                      'that no one else has access to this key'))


      # # Sweep option (only available when online)
      if TheBDM.getState() == BDM_BLOCKCHAIN_READY:
         self.radioSweep = QtWidgets.QRadioButton(self.tr('Sweep any funds owned by this address '
                                         'into your wallet\n'
                                         'Select this option if someone else gave you this key'))
         if self.wlt.watchingOnly:
            self.radioImport.setEnabled(False)
         self.radioSweep.setChecked(True)
      else:
         if TheBDM.getState() in (BDM_OFFLINE, BDM_UNINITIALIZED):
            self.radioSweep = QtWidgets.QRadioButton(self.tr('Sweep any funds owned by this address '
                                            'into your wallet\n'
                                            '(Not available in offline mode)'))
         elif TheBDM.getState() == BDM_SCANNING:
            self.radioSweep = QtWidgets.QRadioButton(self.tr('Sweep any funds owned by this address into your wallet'))
         self.radioImport.setChecked(True)
         self.radioSweep.setEnabled(False)


      sweepTooltip = self.main.createToolTipWidget(self.tr(
         'You should never add an untrusted key to your wallet.  By choosing this '
         'option, you are only moving the funds into your wallet, but not the key '
         'itself.  You should use this option for Casascius physical bitcoins.'))

      importTooltip = self.main.createToolTipWidget(self.tr(
         'This option will make the key part of your wallet, meaning that it '
         'can be used to securely receive future payments.  <b>Never</b> select this '
         'option for private keys that other people may have access to.'))


      # Make sure that there can only be one selection
      btngrp = QtWidgets.QButtonGroup(self)
      btngrp.addButton(self.radioSweep)
      btngrp.addButton(self.radioImport)
      btngrp.setExclusive(True)

      frmWarn = QtWidgets.QFrame()
      frmWarn.setFrameStyle(QtWidgets.QFrame.Box | QtWidgets.QFrame.Plain)
      frmWarnLayout = QtWidgets.QGridLayout()
      frmWarnLayout.addWidget(self.radioSweep, 0, 0, 1, 1)
      frmWarnLayout.addWidget(self.radioImport, 1, 0, 1, 1)
      frmWarnLayout.addWidget(sweepTooltip, 0, 1, 1, 1)
      frmWarnLayout.addWidget(importTooltip, 1, 1, 1, 1)
      frmWarn.setLayout(frmWarnLayout)



      buttonbox = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | \
                                   QtWidgets.QDialogButtonBox.Cancel)
      self.connect(buttonbox, SIGNAL('accepted()'), self.okayClicked)
      self.connect(buttonbox, SIGNAL('rejected()'), self.reject)





      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(frmTop)
      layout.addWidget(self.stackedImport)
      layout.addWidget(frmSP)
      layout.addWidget(frmWarn)
      layout.addWidget(buttonbox)

      self.setWindowTitle(self.tr('Private Key Import'))
      self.setLayout(layout)




   #############################################################################
   def clickImportCount(self):
      isOne = self.radioImportOne.isChecked()
      if isOne:
         self.stackedImport.setCurrentIndex(0)
      else:
         self.stackedImport.setCurrentIndex(1)


   #############################################################################
   def okayClicked(self):
      securePrintCode = None
      if self.chkUseSP.isChecked():
         SECPRINT = HardcodedKeyMaskParams()
         securePrintCode = str(self.edtSecurePrint.text()).strip()
         self.edtSecurePrint.setText("")

         if not checkSecurePrintCode(self, SECPRINT, securePrintCode):
            return

      if self.radioImportOne.isChecked():
         self.processUserString(securePrintCode)
      else:
         self.processMultiSig(securePrintCode)


   #############################################################################
   def processUserString(self, pwd=None):
      theStr = str(self.edtPrivData.text()).strip().replace(' ', '')
      binKeyData, addr160, addrStr = '', '', ''

      try:
         binKeyData, keyType = parsePrivateKeyData(theStr)

         if pwd:
            SECPRINT = HardcodedKeyMaskParams()
            maskKey = SECPRINT['FUNC_KDF'](pwd)
            SBDbinKeyData = SECPRINT['FUNC_UNMASK'](SecureBinaryData(binKeyData), ekey=maskKey)
            binKeyData = SBDbinKeyData.toBinStr()
            SBDbinKeyData.destroy()

         zeroes32 = '\x00'*32
         if binKeyData==zeroes32:
            QtWidgets.QMessageBox.critical(self, self.tr('Invalid Private Key'), self.tr(
               'You entered all zeros.  This is not a valid private key!'),
               QtWidgets.QMessageBox.Ok)
            LOGERROR('User attempted import of private key 0x00*32')
            return

         if binary_to_int(binKeyData, BIGENDIAN) >= SECP256K1_ORDER:
            QtWidgets.QMessageBox.critical(self, self.tr('Invalid Private Key'), self.tr(
               'The private key you have entered is actually not valid '
               'for the elliptic curve used by Bitcoin (secp256k1). '
               'Almost any 64-character hex is a valid private key '
               '<b>except</b> for those greater than: '
               '<br><br>'
               'fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141'
               '<br><br>'
               'Please try a different private key.'), QtWidgets.QMessageBox.Ok)
            LOGERROR('User attempted import of invalid private key!')
            return

         addr160 = convertKeyDataToAddress(privKey=binKeyData)
         addrStr = hash160_to_addrStr(addr160)

      except InvalidHashError as e:
         QtWidgets.QMessageBox.warning(self, self.tr('Entry Error'), self.tr(
            'The private key data you supplied appears to '
            'contain a consistency check.  This consistency '
            'check failed.  Please verify you entered the '
            'key data correctly.'), QtWidgets.QMessageBox.Ok)
         LOGERROR('Private key consistency check failed.')
         return
      except BadInputError as e:
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid Data'), self.tr('Something went terribly '
            'wrong!  (key data unrecognized)'), QtWidgets.QMessageBox.Ok)
         LOGERROR('Unrecognized key data!')
         return
      except CompressedKeyError as e:
         QtWidgets.QMessageBox.critical(self, self.tr('Unsupported key type'), self.tr('You entered a key '
            'for an address that uses a compressed public key, usually produced '
            'in Bitcoin Core/bitcoind wallets created after version 0.6.0.  Armory '
            'does not yet support this key type.'))
         LOGERROR('Compressed key data recognized but not supported')
         return
      except:
         QtWidgets.QMessageBox.critical(self, self.tr('Error Processing Key'), self.tr(
            'There was an error processing the private key data. '
            'Please check that you entered it correctly'), QtWidgets.QMessageBox.Ok)
         LOGEXCEPT('Error processing the private key data')
         return



      if not 'mini' in keyType.lower():
         reply = QtWidgets.QMessageBox.question(self, self.tr('Verify Address'), self.tr(
               'The key data you entered appears to correspond to '
               'the following Bitcoin address:\n\n %s '
               '\n\nIs this the correct address?' % addrStr),
               QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No | QtWidgets.QMessageBox.Cancel)
         if reply == QtWidgets.QMessageBox.Cancel:
            return
         else:
            if reply == QtWidgets.QMessageBox.No:
               binKeyData = binary_switchEndian(binKeyData)
               addr160 = convertKeyDataToAddress(privKey=binKeyData)
               addrStr = hash160_to_addrStr(addr160)
               reply = QtWidgets.QMessageBox.question(self, self.tr('Try Again'), self.tr(
                     'It is possible that the key was supplied in a '
                     '"reversed" form.  When the data you provide is '
                     'reversed, the following address is obtained:\n\n '
                     '%s \n\nIs this the correct address?' % addrStr), \
                     QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)
               if reply == QtWidgets.QMessageBox.No:
                  binKeyData = ''
                  return

      # Finally, let's add the address to the wallet, or sweep the funds
      if self.radioSweep.isChecked():
         if self.wlt.hasAddr(addr160):
            result = QtWidgets.QMessageBox.warning(self, 'Duplicate Address', \
            'The address you are trying to sweep is already part of this '
            'wallet.  You can still sweep it to a new address, but it will '
            'have no effect on your overall balance (in fact, it might be '
            'negative if you have to pay a fee for the transfer)\n\n'
            'Do you still want to sweep this key?', \
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.Cancel)
            if not result == QtWidgets.QMessageBox.Yes:
               return

         else:
            wltID = self.main.getWalletForAddrHash(addr160)
            if not wltID == '':
               addr = self.main.walletMap[wltID].addrMap[addr160]
               typ = 'Imported' if addr.chainIndex == -2 else 'Permanent'
               msg = ('The key you entered is already part of another wallet you '
                      'are maintaining:'
                     '<br><br>'
                     '<b>Address</b>: ' + addrStr + '<br>'
                     '<b>Wallet ID</b>: ' + wltID + '<br>'
                     '<b>Wallet Name</b>: ' + self.main.walletMap[wltID].labelName + '<br>'
                     '<b>Address Type</b>: ' + typ +
                     '<br><br>'
                     'The sweep operation will simply move bitcoins out of the wallet '
                     'above into this wallet.  If the network charges a fee for this '
                     'transaction, you balance will be reduced by that much.')
               result = QtWidgets.QMessageBox.warning(self, 'Duplicate Address', msg, \
                     QtWidgets.QMessageBox.Ok | QtWidgets.QMessageBox.Cancel)
               if not result == QtWidgets.QMessageBox.Ok:
                  return

         # Create the address object for the addr to be swept
         sweepAddrList = []
         sweepAddrList.append(PyBtcAddress().createFromPlainKeyData(SecureBinaryData(binKeyData)))
         self.wlt.sweepAddressList(sweepAddrList, self.main)

         # Regardless of the user confirmation, we're done here
         self.accept()

      elif self.radioImport.isChecked():
         if self.wlt.hasAddr(addr160):
            QtWidgets.QMessageBox.critical(self, 'Duplicate Address', \
            'The address you are trying to import is already part of your '
            'wallet.  Address cannot be imported', QtWidgets.QMessageBox.Ok)
            return

         wltID = self.main.getWalletForAddrHash(addr160)
         if not wltID == '':
            addr = self.main.walletMap[wltID].addrMap[addr160]
            typ = 'Imported' if addr.chainIndex == -2 else 'Permanent'
            msg = self.tr('The key you entered is already part of another wallet you own:'
                   '<br><br>'
                   '<b>Address</b>: ' + addrStr + '<br>'
                   '<b>Wallet ID</b>: ' + wltID + '<br>'
                   '<b>Wallet Name</b>: ' + self.main.walletMap[wltID].labelName + '<br>'
                   '<b>Address Type</b>: ' + typ +
                   '<br><br>'
                   'Armory cannot properly display balances or create transactions '
                   'when the same address is in multiple wallets at once.  ')
            if typ == 'Imported':
               QtWidgets.QMessageBox.critical(self, 'Duplicate Addresses', \
                  msg + 'To import this address to this wallet, please remove it from the '
                  'other wallet, then try the import operation again.', QtWidgets.QMessageBox.Ok)
            else:
               QtWidgets.QMessageBox.critical(self, 'Duplicate Addresses', \
                  msg + 'Additionally, this address is mathematically linked '
                  'to its wallet (permanently) and cannot be deleted or '
                  'imported to any other wallet.  The import operation cannot '
                  'continue.', QtWidgets.QMessageBox.Ok)
            return

         if self.wlt.useEncryption and self.wlt.isLocked:
            dlg = DlgUnlockWallet(self.wlt, self, self.main, 'Encrypt New Address')
            if not dlg.exec_():
               reply = QtWidgets.QMessageBox.critical(self, 'Wallet is locked',
                  'New private key data cannot be imported unless the wallet is '
                  'unlocked.  Please try again when you have the passphrase.', \
                  QtWidgets.QMessageBox.Ok)
               return


         self.wlt.importExternalAddressData(privKey=SecureBinaryData(binKeyData))
         self.main.statusBar().showMessage('Successful import of address ' \
                                 + addrStr + ' into wallet ' + self.wlt.uniqueIDB58, 10000)

      try:
         self.parent.wltAddrModel.reset()
      except:
         pass

      self.accept()
      self.main.loadCppWallets()


   #############################################################################
   def processMultiSig(self, pwd=None):
      thisWltID = self.wlt.uniqueIDB58

      inputText = str(self.txtPrivBulk.toPlainText())
      inputLines = [s.strip().replace(' ', '') for s in inputText.split('\n')]
      binKeyData, addr160, addrStr = '', '', ''

      if pwd:
         SECPRINT = HardcodedKeyMaskParams()
         maskKey = SECPRINT['FUNC_KDF'](pwd)

      privKeyList = []
      addrSet = set()
      nLines = 0
      for line in inputLines:
         if 'PublicX' in line or 'PublicY' in line:
            continue
         lineend = line.split(':')[-1]
         try:
            nLines += 1
            binKeyData = SecureBinaryData(parsePrivateKeyData(lineend)[0])
            if pwd: binKeyData = SECPRINT['FUNC_UNMASK'](binKeyData, ekey=maskKey)

            addr160 = convertKeyDataToAddress(privKey=binKeyData.toBinStr())
            if not addr160 in addrSet:
               addrSet.add(addr160)
               addrStr = hash160_to_addrStr(addr160)
               privKeyList.append([addr160, addrStr, binKeyData])
         except:
            LOGWARN('Key line skipped, probably not a private key (key not shown for security)')
            continue

      if len(privKeyList) == 0:
         if nLines > 1:
            QtWidgets.QMessageBox.critical(self, 'Invalid Data', \
               'No valid private key data was entered.', QtWidgets.QMessageBox.Ok)
         return

      # privKeyList now contains:
      #  [ [A160, AddrStr, Priv],
      #    [A160, AddrStr, Priv],
      #    [A160, AddrStr, Priv], ... ]
      # Determine if any addresses are already part of some wallets
      addr_to_wltID = lambda a: self.main.getWalletForAddrHash(a)
      allWltList = [ [addr_to_wltID(k[0]), k[1]] for k in privKeyList]
      # allWltList is now [ [WltID, AddrStr], [WltID, AddrStr], ... ]


      if self.radioSweep.isChecked():
         ##### SWEEPING #####
         dupeWltList = filter(lambda a: len(a[0]) > 0, allWltList)
         if len(dupeWltList) > 0:
            reply = QtWidgets.QMessageBox.critical(self, self.tr('Duplicate Addresses!'), self.tr(
               'You are attempting to sweep %d addresses, but %d of them '
               'are already part of existing wallets.  That means that some or '
               'all of the bitcoins you sweep may already be owned by you. '
               '<br><br>'
               'Would you like to continue anyway?' % (len(allWltList), len(dupeWltList))), \
               QtWidgets.QMessageBox.Ok | QtWidgets.QMessageBox.Cancel)
            if reply == QtWidgets.QMessageBox.Cancel:
               return


         #create address less to sweep
         addrList = []
         for addr160, addrStr, SecurePriv in privKeyList:
            pyAddr = PyBtcAddress().createFromPlainKeyData(SecurePriv)
            addrList.append(pyAddr)

         #get PyBtcWallet object
         self.wlt.sweepAddressList(addrList, self.main)

      else:
         ##### IMPORTING #####

         # allWltList is [ [WltID, AddrStr], [WltID, AddrStr], ... ]

         # Warn about addresses that would be duplicates.
         # Addresses already in the selected wallet will simply be skipped, no
         # need to do anything about that -- only addresses that would appear in
         # two wlts if we were to continue.
         dupeWltList = filter(lambda a: (len(a[0]) > 0 and a[0] != thisWltID), allWltList)
         if len(dupeWltList) > 0:
            dupeAddrStrList = [d[1] for d in dupeWltList]
            dlg = DlgDuplicateAddr(dupeAddrStrList, self, self.main)

            if not dlg.exec_():
               return

            privKeyList = filter(lambda x: (x[1] not in dupeAddrStrList), privKeyList)


         # Confirm import
         addrStrList = [k[1] for k in privKeyList]
         dlg = DlgConfirmBulkImport(addrStrList, thisWltID, self, self.main)
         if not dlg.exec_():
            return

         if self.wlt.useEncryption and self.wlt.isLocked:
            # Target wallet is encrypted...
            unlockdlg = DlgUnlockWallet(self.wlt, self, self.main, self.tr('Unlock Wallet to Import'))
            if not unlockdlg.exec_():
               QtWidgets.QMessageBox.critical(self, self.tr('Wallet is Locked'), self.tr(
                  'Cannot import private keys without unlocking wallet!'), \
                  QtWidgets.QMessageBox.Ok)
               return


         nTotal = 0
         nImport = 0
         nAlready = 0
         nError = 0
         privKeyToImport = []
         for addr160, addrStr, sbdKey in privKeyList:
            nTotal += 1
            try:
               if not self.main.getWalletForAddrHash(addr160) == thisWltID:
                  privKeyToImport.append([sbdKey, addr160])
                  nImport += 1
               else:
                  nAlready += 1
            except Exception as msg:
               # print '***ERROR importing:', addrStr
               # print '         Error Msg:', msg
               # nError += 1
               LOGERROR('Problem importing: %s: %s', addrStr, msg)
               raise

         self.wlt.importExternalAddressBatch(privKeyToImport)

         if nAlready == nTotal:
            MsgBoxCustom(MSGBOX.Warning, self.tr('Nothing Imported!'), self.tr('All addresses '
               'chosen to be imported are already part of this wallet. '
               'Nothing was imported.'))
            return
         elif nImport == 0 and nTotal > 0:
            MsgBoxCustom(MSGBOX.Error, self.tr('Error!'), self.tr(
               'Failed:  No addresses could be imported. '
               'Please check the logfile (ArmoryQt.exe.log) or the console output '
               'for information about why it failed.'))
            return
         else:
            if nError == 0:
               if nAlready > 0:
                  MsgBoxCustom(MSGBOX.Good, self.tr('Success!'), self.tr(
                     'Success: %d private keys were imported into your wallet. '
                     '<br><br>'
                     'The other %d private keys were skipped, because they were '
                     'already part of your wallet.' % (nImport, nAlready)))
               else:
                  MsgBoxCustom(MSGBOX.Good, self.tr('Success!'), self.tr(
                     'Success: %d private keys were imported into your wallet.' % nImport))
            else:
               MsgBoxCustom(MSGBOX.Warning, self.tr('Partial Success!'), self.tr(
                  '%d private keys were imported into your wallet, but there were '
                  'also %d addresses that could not be imported (see console '
                  'or log file for more information).  It is safe to try this '
                  'operation again: all addresses previously imported will be '
                  'skipped.' % (nImport, nError)))

      try:
         self.parent.wltAddrModel.reset()
      except AttributeError:
         pass

      self.accept()
      self.main.loadCppWallets()


#############################################################################
class DlgVerifySweep(ArmoryDialog):
   def __init__(self, inputStr, outputStr, outVal, fee, parent=None, main=None):
      super(DlgVerifySweep, self).__init__(parent, main)


      lblQuestion = QRichLabel(self.tr(
            'You are about to <i>sweep</i> all funds from the specified address '
            'to your wallet.  Please confirm the action:'))


      outStr = coin2str(outVal, maxZeros=2)
      feeStr = ('') if (fee == 0) else (self.tr('(Fee: %s)' % coin2str(fee, maxZeros=0)))

      frm = QtWidgets.QFrame()
      frm.setFrameStyle(STYLE_RAISED)
      frmLayout = QtWidgets.QGridLayout()
      # frmLayout.addWidget(QRichLabel('Funds will be <i>swept</i>...'), 0,0, 1,2)
      frmLayout.addWidget(QRichLabel(self.tr('      From %s' % inputStr), doWrap=False), 1, 0, 1, 2)
      frmLayout.addWidget(QRichLabel(self.tr('      To %s' % outputStr), doWrap=False), 2, 0, 1, 2)
      frmLayout.addWidget(QRichLabel(self.tr('      Total <b>%s</b> BTC %s' % (outStr, feeStr)), doWrap=False), 3, 0, 1, 2)
      frm.setLayout(frmLayout)

      lblFinalConfirm = QtWidgets.QLabel(self.tr('Are you sure you want to execute this transaction?'))

      bbox = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | \
                              QtWidgets.QDialogButtonBox.Cancel)
      self.connect(bbox, SIGNAL('accepted()'), self.accept)
      self.connect(bbox, SIGNAL('rejected()'), self.reject)

      lblWarnImg = QtWidgets.QLabel()
      lblWarnImg.setPixmap(QtGui.QPixmap('./img/MsgBox_warning48.png'))
      lblWarnImg.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)

      layout = QtWidgets.QHBoxLayout()
      layout.addWidget(lblWarnImg)
      layout.addWidget(makeLayoutFrame(VERTICAL, [lblQuestion, frm, lblFinalConfirm, bbox]))
      self.setLayout(layout)

      self.setWindowTitle(self.tr('Confirm Sweep'))




##############################################################################
class DlgConfirmBulkImport(ArmoryDialog):
   def __init__(self, addrList, wltID, parent=None, main=None):
      super(DlgConfirmBulkImport, self).__init__(parent, main)

      self.wltID = wltID

      if len(addrList) == 0:
         QtWidgets.QMessageBox.warning(self, self.tr('No Addresses to Import'), self.tr(
           'There are no addresses to import!'), QtWidgets.QMessageBox.Ok)
         self.reject()


      walletDescr = self.tr('a new wallet')
      if not wltID == None:
         wlt = self.main.walletMap[wltID]
         walletDescr = self.tr('wallet, <b>%s</b> (%s)' % (wltID, wlt.labelName))
      lblDescr = QRichLabel(self.tr(
         'You are about to import <b>%d</b> addresses into %s.<br><br> '
         'The following is a list of addresses to be imported:' % (len(addrList), walletDescr)))

      fnt = GETFONT('Fixed', 10)
      w, h = tightSizeNChar(fnt, 100)
      txtDispAddr = QtWidgets.QTextEdit()
      txtDispAddr.setFont(fnt)
      txtDispAddr.setReadOnly(True)
      txtDispAddr.setMinimumWidth(min(w, 700))
      txtDispAddr.setMinimumHeight(16.2 * h)
      txtDispAddr.setText('\n'.join(addrList))

      buttonBox = QtWidgets.QDialogButtonBox()
      self.btnAccept = QtWidgets.QPushButton(self.tr("Import"))
      self.btnReject = QtWidgets.QPushButton(self.tr("Cancel"))
      self.connect(self.btnAccept, SIGNAL(CLICKED), self.accept)
      self.connect(self.btnReject, SIGNAL(CLICKED), self.reject)
      buttonBox.addButton(self.btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnReject, QtWidgets.QDialogButtonBox.RejectRole)

      dlgLayout = QtWidgets.QVBoxLayout()
      dlgLayout.addWidget(lblDescr)
      dlgLayout.addWidget(txtDispAddr)
      dlgLayout.addWidget(buttonBox)
      self.setLayout(dlgLayout)

      self.setWindowTitle(self.tr('Confirm Import'))
      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))


#############################################################################
class DlgDuplicateAddr(ArmoryDialog):
   def __init__(self, addrList, wlt, parent=None, main=None):
      super(DlgDuplicateAddr, self).__init__(parent, main)

      self.wlt = wlt
      self.doCancel = True
      self.newOnly = False

      if len(addrList) == 0:
         QtWidgets.QMessageBox.warning(self, self.tr('No Addresses to Import'), \
           self.tr('There are no addresses to import!'), QtWidgets.QMessageBox.Ok)
         self.reject()

      lblDescr = QRichLabel(self.tr(
         '<font color=%s>Duplicate addresses detected!</font> The following '
         'addresses already exist in other Armory wallets:' % htmlColor('TextWarn')))

      fnt = GETFONT('Fixed', 8)
      w, h = tightSizeNChar(fnt, 50)
      txtDispAddr = QtWidgets.QTextEdit()
      txtDispAddr.setFont(fnt)
      txtDispAddr.setReadOnly(True)
      txtDispAddr.setMinimumWidth(w)
      txtDispAddr.setMinimumHeight(8.2 * h)
      txtDispAddr.setText('\n'.join(addrList))

      lblWarn = QRichLabel(self.tr(
         'Duplicate addresses cannot be imported.  If you continue, '
         'the addresses above will be ignored, and only new addresses '
         'will be imported to this wallet.'))

      buttonBox = QtWidgets.QDialogButtonBox()
      self.btnContinue = QtWidgets.QPushButton(self.tr("Continue"))
      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.connect(self.btnContinue, SIGNAL(CLICKED), self.accept)
      self.connect(self.btnCancel, SIGNAL(CLICKED), self.reject)
      buttonBox.addButton(self.btnContinue, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)

      dlgLayout = QtWidgets.QVBoxLayout()
      dlgLayout.addWidget(lblDescr)
      dlgLayout.addWidget(txtDispAddr)
      dlgLayout.addWidget(lblWarn)
      dlgLayout.addWidget(buttonBox)
      self.setLayout(dlgLayout)

      self.setWindowTitle(self.tr('Duplicate Addresses'))

#############################################################################
class DlgImportPaperWallet(ArmoryDialog):

   def __init__(self, parent=None, main=None):
      super(DlgImportPaperWallet, self).__init__(parent, main)

      self.wltDataLines = [[]] * 4
      self.prevChars = [''] * 4

      for i, edt in enumerate(self.lineEdits):
         # I screwed up the ref/copy, this loop only connected the last one...
         # theSlot = lambda: self.autoSpacerFunction(i)
         # self.connect(edt, SIGNAL('textChanged(QString)'), theSlot)
         edt.setMinimumWidth(tightSizeNChar(edt, 50)[0])

      # Just do it manually because it's guaranteed to work!
      slot = lambda: self.autoSpacerFunction(0)
      self.connect(self.lineEdits[0], SIGNAL('textEdited(QString)'), slot)

      slot = lambda: self.autoSpacerFunction(1)
      self.connect(self.lineEdits[1], SIGNAL('textEdited(QString)'), slot)

      slot = lambda: self.autoSpacerFunction(2)
      self.connect(self.lineEdits[2], SIGNAL('textEdited(QString)'), slot)

      slot = lambda: self.autoSpacerFunction(3)
      self.connect(self.lineEdits[3], SIGNAL('textEdited(QString)'), slot)

      buttonbox = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | \
                                   QtWidgets.QDialogButtonBox.Cancel)
      self.connect(buttonbox, SIGNAL('accepted()'), self.verifyUserInput)
      self.connect(buttonbox, SIGNAL('rejected()'), self.reject)

      self.labels = [QtWidgets.QLabel() for i in range(4)]
      self.labels[0].setText(self.tr('Root Key:'))
      self.labels[1].setText('')
      self.labels[2].setText(self.tr('Chain Code:'))
      self.labels[3].setText('')

      lblDescr1 = QtWidgets.QLabel(self.tr(
          'Enter the characters exactly as they are printed on the '
          'paper-backup page.  Alternatively, you can scan the QR '
          'code from another application, then copy&paste into the '
          'entry boxes below.'))
      lblDescr2 = QtWidgets.QLabel(self.tr(
          'The data can be entered <i>with</i> or <i>without</i> '
          'spaces, and up to '
          'one character per line will be corrected automatically.'))
      for lbl in (lblDescr1, lblDescr2):
         lbl.setTextFormat(QtCore.Qt.RichText)
         lbl.setWordWrap(True)

      layout = QtWidgets.QGridLayout()
      layout.addWidget(lblDescr1, 0, 0, 1, 2)
      layout.addWidget(lblDescr2, 1, 0, 1, 2)
      for i, edt in enumerate(self.lineEdits):
         layout.addWidget(self.labels[i], i + 2, 0)
         layout.addWidget(self.lineEdits[i], i + 2, 1)

      self.chkEncrypt = QtWidgets.QCheckBox(self.tr('Encrypt Wallet'))
      self.chkEncrypt.setChecked(True)

      bottomFrm = makeHorizFrame([self.chkEncrypt, buttonbox])
      layout.addWidget(bottomFrm, 6, 0, 1, 2)
      layout.setVerticalSpacing(10)
      self.setLayout(layout)


      self.setWindowTitle(self.tr('Recover Wallet from Paper Backup'))
      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))


   def autoSpacerFunction(self, i):
      currStr = str(self.lineEdits[i].text())
      rawStr = currStr.replace(' ', '')
      if len(rawStr) > 36:
         rawStr = rawStr[:36]

      if len(rawStr) == 36:
         quads = [rawStr[j:j + 4] for j in range(0, 36, 4)]
         self.lineEdits[i].setText(' '.join(quads))


   def verifyUserInput(self):
      def englishNumberList(nums):
         nums = map(str, nums)
         if len(nums) == 1:
            return nums[0]
         return ', '.join(nums[:-1]) + ' and ' + nums[-1]

      errorLines = []
      for i in range(4):
         hasError = False
         try:
            data, err = readSixteenEasyBytes(str(self.lineEdits[i].text()))
         except (KeyError, TypeError):
            data, err = ('', 'Exception')

         if data == '':
            reply = QtWidgets.QMessageBox.critical(self, self.tr('Verify Wallet ID'), self.tr(
               'There is an error on line %d of the data you '
               'entered, which could not be fixed automatically.  Please '
               'double-check that you entered the text exactly as it appears '
               'on the wallet-backup page.' % (i + 1)),
               QtWidgets.QMessageBox.Ok)
            LOGERROR('Error in wallet restore field')
            self.labels[i].setText('<font color="red">' + str(self.labels[i].text()) + '</font>')
            return
         if err == 'Fixed_1' or err == 'No_Checksum':
            errorLines += [i + 1]

         self.wltDataLines[i] = data

      if errorLines:
         pluralChar = '' if len(errorLines) == 1 else 's'
         article = ' an' if len(errorLines) == 1 else ''
         QtWidgets.QMessageBox.question(self, self.tr('Errors Corrected!'), self.tr(
            'Detected %d error(s) on line(s) %s '
            'in the data you entered.  Armory attempted to fix the '
            'error(s) but it is not always right.  Be sure '
            'to verify the "Wallet Unique ID" closely on the next window.' % (len(errorLines),
               englishNumberList(errorLines))), "", QtWidgets.QMessageBox.Ok)

      # If we got here, the data is valid, let's create the wallet and accept the dlg
      privKey = ''.join(self.wltDataLines[:2])
      chain = ''.join(self.wltDataLines[2:])

      root = PyBtcAddress().createFromPlainKeyData(SecureBinaryData(privKey))
      root.chaincode = SecureBinaryData(chain)
      first = root.extendAddressChain()
      newWltID = binary_to_base58((ADDRBYTE + first.getAddr160()[:5])[::-1])

      if newWltID in self.main.walletMap:
         QtWidgets.QMessageBox.question(self, self.tr('Duplicate Wallet!'), self.tr(
               'The data you entered is for a wallet with a ID: \n\n %s '
               '\n\nYou already own this wallet! \n  '
               'Nothing to do...' % newWltID), QtWidgets.QMessageBox.Ok)
         self.reject()
         return



      reply = QtWidgets.QMessageBox.question(self, self.tr('Verify Wallet ID'), self.tr(
               'The data you entered corresponds to a wallet with a wallet ID: \n\n '
               '%s \n\nDoes this ID match the "Wallet Unique ID" '
               'printed on your paper backup?  If not, click "No" and reenter '
               'key and chain-code data again.' % newWltID), \
               QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)
      if reply == QtWidgets.QMessageBox.No:
         return

      passwd = []
      if self.chkEncrypt.isChecked():
         dlgPasswd = DlgChangePassphrase(self, self.main)
         if dlgPasswd.exec_():
            passwd = SecureBinaryData(str(dlgPasswd.edtPasswd1.text()))
         else:
            QtWidgets.QMessageBox.critical(self, self.tr('Cannot Encrypt'), self.tr(
               'You requested your restored wallet be encrypted, but no '
               'valid passphrase was supplied.  Aborting wallet recovery.'), \
               QtWidgets.QMessageBox.Ok)
            return

      if passwd:
         self.newWallet = PyBtcWallet().createNewWallet(\
                                 plainRootKey=SecureBinaryData(privKey), \
                                 chaincode=SecureBinaryData(chain), \
                                 shortLabel=self.tr('PaperBackup - %s' % newWltID), \
                                 withEncrypt=True, \
                                 securePassphrase=passwd, \
                                 kdfTargSec=0.25, \
                                 kdfMaxMem=32 * 1024 * 1024, \
                                 isActuallyNew=False, \
                                 doRegisterWithBDM=False)
      else:
         self.newWallet = PyBtcWallet().createNewWallet(\
                                 plainRootKey=SecureBinaryData(privKey), \
                                 chaincode=SecureBinaryData(chain), \
                                 shortLabel=self.tr('PaperBackup - %s' % newWltID), \
                                 withEncrypt=False, \
                                 isActuallyNew=False, \
                                 doRegisterWithBDM=False)

      def fillAddrPoolAndAccept():
         progressBar = DlgProgress(self, self.main, None, HBar=1,
                                   Title=self.tr("Computing New Addresses"))
         progressBar.exec_(self.newWallet.fillAddressPool)
         self.accept()

      # Will pop up a little "please wait..." window while filling addr pool
      DlgExecLongProcess(fillAddrPoolAndAccept, self.tr("Recovering wallet..."), self, self.main).exec_()

################################################################################
class DlgRemoveWallet(ArmoryDialog):
   def __init__(self, wlt, parent=None, main=None):
      super(DlgRemoveWallet, self).__init__(parent, main)

      wltID = wlt.uniqueIDB58
      wltName = wlt.labelName
      wltDescr = wlt.labelDescr
      lblWarning = QtWidgets.QLabel(self.tr('<b>!!! WARNING !!!</b>\n\n'))
      lblWarning.setTextFormat(QtCore.Qt.RichText)
      lblWarning.setAlignment(QtCore.Qt.AlignHCenter)

      lblWarning2 = QtWidgets.QLabel(self.tr('<i>You have requested that the following wallet '
                            'be removed from Armory:</i>'))
      lblWarning.setTextFormat(QtCore.Qt.RichText)
      lblWarning.setWordWrap(True)
      lblWarning.setAlignment(QtCore.Qt.AlignHCenter)

      lbls = []
      lbls.append([])
      lbls[0].append(QtWidgets.QLabel(self.tr('Wallet Unique ID:')))
      lbls[0].append(QtWidgets.QLabel(wltID))
      lbls.append([])
      lbls[1].append(QtWidgets.QLabel(self.tr('Wallet Name:')))
      lbls[1].append(QtWidgets.QLabel(wlt.labelName))
      lbls.append([])
      lbls[2].append(QtWidgets.QLabel(self.tr('Description:')))
      lbls[2].append(QtWidgets.QLabel(wlt.labelDescr))
      lbls[2][-1].setWordWrap(True)


      # TODO:  This should not *ever* require a blockchain scan, because all
      #        current wallets should already be registered and up-to-date.
      #        But I should verify that this is actually the case.
      wltEmpty = True
      if TheBDM.getState() == BDM_BLOCKCHAIN_READY:
         # Removed this line of code because it's part of the old BDM paradigm.
         # Leaving this comment here in case it needs to be replaced by anything
         # wlt.syncWithBlockchainLite()
         bal = wlt.getBalance('Full')
         lbls.append([])
         lbls[3].append(QtWidgets.QLabel(self.tr('Current Balance (w/ unconfirmed):')))
         if bal > 0:
            lbls[3].append(QtWidgets.QLabel('<font color="red"><b>' + coin2str(bal, maxZeros=1).strip() + ' BTC</b></font>'))
            lbls[3][-1].setTextFormat(QtCore.Qt.RichText)
            wltEmpty = False
         else:
            lbls[3].append(QtWidgets.QLabel(coin2str(bal, maxZeros=1) + ' BTC'))


      # Add two WARNING images on either side of dialog
      lblWarnImg = QtWidgets.QLabel()
      lblWarnImg.setPixmap(QtGui.QPixmap('./img/MsgBox_warning48.png'))
      lblWarnImg.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
      lblWarnImg2 = QtWidgets.QLabel()
      lblWarnImg2.setPixmap(QtGui.QPixmap('./img/MsgBox_warning48.png'))
      lblWarnImg2.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)

      # Add the warning text and images to the top of the dialog
      layout = QtWidgets.QGridLayout()
      layout.addWidget(lblWarning, 0, 1, 1, 1)
      layout.addWidget(lblWarning2, 1, 1, 1, 1)
      layout.addWidget(lblWarnImg, 0, 0, 2, 1)
      layout.addWidget(lblWarnImg2, 0, 2, 2, 1)

      frmInfo = QtWidgets.QFrame()
      frmInfo.setFrameStyle(QtWidgets.QFrame.Box | QtWidgets.QFrame.Plain)
      frmInfo.setLineWidth(3)
      frmInfoLayout = QtWidgets.QGridLayout()
      for i in range(len(lbls)):
         lbls[i][0].setText('<b>' + lbls[i][0].text() + '</b>')
         lbls[i][0].setTextFormat(QtCore.Qt.RichText)
         frmInfoLayout.addWidget(lbls[i][0], i, 0)
         frmInfoLayout.addWidget(lbls[i][1], i, 1, 1, 2)

      frmInfo.setLayout(frmInfoLayout)
      layout.addWidget(frmInfo, 2, 0, 2, 3)
      hasWarningRow = False
      if not wlt.watchingOnly:
         if not wltEmpty:
            lbl = QRichLabel(self.tr('<b>WALLET IS NOT EMPTY.  Only delete this wallet if you '
                             'have a backup on paper or saved to a another location '
                             'outside your settings directory.</b>'))
            hasWarningRow = True
         elif wlt.isWltSigningAnyLockbox(self.main.allLockboxes):
            lbl = QRichLabel(self.tr('<b>WALLET IS PART OF A LOCKBOX.  Only delete this wallet if you '
                             'have a backup on paper or saved to a another location '
                             'outside your settings directory.</b>'))
            hasWarningRow = True
         if hasWarningRow:
            lbls.append(lbl)
            layout.addWidget(lbl, 4, 0, 1, 3)

      self.radioDelete = QtWidgets.QRadioButton(self.tr('Permanently delete this wallet'))
      self.radioWatch = QtWidgets.QRadioButton(self.tr('Delete private keys only, make watching-only'))

      # Make sure that there can only be one selection
      btngrp = QtWidgets.QButtonGroup(self)
      btngrp.addButton(self.radioDelete)
      if not self.main.usermode == USERMODE.Standard:
         btngrp.addButton(self.radioWatch)
      btngrp.setExclusive(True)

      ttipDelete = self.main.createToolTipWidget(self.tr(
         'This will delete the wallet file, removing '
         'all its private keys from your settings directory. '
         'If you intend to keep using addresses from this '
         'wallet, do not select this option unless the wallet '
         'is backed up elsewhere.'))
      ttipWatch = self.main.createToolTipWidget(self.tr(
         'This will delete the private keys from your wallet, '
         'leaving you with a watching-only wallet, which can be '
         'used to generate addresses and monitor incoming '
         'payments.  This option would be used if you created '
         'the wallet on this computer <i>in order to transfer '
         'it to a different computer or device and want to '
         'remove the private data from this system for security.</i>'))


      self.chkPrintBackup = QtWidgets.QCheckBox(self.tr(
         'Print a paper backup of this wallet before deleting'))

      if wlt.watchingOnly:
         ttipDelete = self.main.createToolTipWidget(self.tr(
            'This will delete the wallet file from your system. '
            'Since this is a watching-only wallet, no private keys '
            'will be deleted.'))
         ttipWatch = self.main.createToolTipWidget(self.tr(
            'This wallet is already a watching-only wallet so this option '
            'is pointless'))
         self.radioWatch.setEnabled(False)
         self.chkPrintBackup.setEnabled(False)


      self.frm = []

      rdoFrm = QtWidgets.QFrame()
      rdoFrm.setFrameStyle(STYLE_RAISED)
      rdoLayout = QtWidgets.QGridLayout()

      startRow = 0
      for rdo, ttip in [(self.radioDelete, ttipDelete), \
                       (self.radioWatch, ttipWatch)]:
         self.frm.append(QtWidgets.QFrame())
         # self.frm[-1].setFrameStyle(STYLE_SUNKEN)
         self.frm[-1].setFrameStyle(QtWidgets.QFrame.NoFrame)
         frmLayout = QtWidgets.QHBoxLayout()
         frmLayout.addWidget(rdo)
         ttip.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
         frmLayout.addWidget(ttip)
         frmLayout.addStretch()
         self.frm[-1].setLayout(frmLayout)
         rdoLayout.addWidget(self.frm[-1], startRow, 0, 1, 3)
         startRow += 1


      self.radioDelete.setChecked(True)
      rdoFrm.setLayout(rdoLayout)

      startRow = 6 if not hasWarningRow else 5
      layout.addWidget(rdoFrm, startRow, 0, 1, 3)

      if wlt.watchingOnly:
         self.frm[-1].setVisible(False)


      printTtip = self.main.createToolTipWidget(self.tr(
         'If this box is checked, you will have the ability to print off an '
         'unencrypted version of your wallet before it is deleted.  <b>If '
         'printing is unsuccessful, please press *CANCEL* on the print dialog '
         'to prevent the delete operation from continuing</b>'))
      printFrm = makeLayoutFrame(HORIZONTAL, [self.chkPrintBackup, \
                                              printTtip, \
                                              'Stretch'])
      startRow += 1
      layout.addWidget(printFrm, startRow, 0, 1, 3)

      if wlt.watchingOnly:
         printFrm.setVisible(False)


      rmWalletSlot = lambda: self.removeWallet(wlt)

      startRow += 1
      self.btnDelete = QtWidgets.QPushButton(self.tr("Delete"))
      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.connect(self.btnDelete, SIGNAL(CLICKED), rmWalletSlot)
      self.connect(self.btnCancel, SIGNAL(CLICKED), self.reject)
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnDelete, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)
      layout.addWidget(buttonBox, startRow, 0, 1, 3)

      self.setLayout(layout)
      self.setWindowTitle(self.tr('Delete Wallet Options'))


   def removeWallet(self, wlt):

      # Open the print dialog.  If they hit cancel at any time, then
      # we go back to the primary wallet-remove dialog without any other action
      if self.chkPrintBackup.isChecked():
         if not OpenPaperBackupDialog('Single', self, self.main, wlt, \
                                                self.tr('Unlock Paper Backup')):
            QtWidgets.QMessageBox.warning(self, self.tr('Operation Aborted'), self.tr(
              'You requested a paper backup before deleting the wallet, but '
              'clicked "Cancel" on the backup printing window.  So, the delete '
              'operation was canceled as well.'), QtWidgets.QMessageBox.Ok)
            return


      # If they only want to exclude the wallet, we will add it to the excluded
      # list and remove it from the application.  The wallet files will remain
      # in the settings directory but will be ignored by Armory

      wltID = wlt.uniqueIDB58
      if wlt.watchingOnly:
         reply = QtWidgets.QMessageBox.warning(self, self.tr('Confirm Delete'), \
         self.tr('You are about to delete a watching-only wallet.  Are you sure '
         'you want to do this?'), QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.Cancel)
      elif self.radioDelete.isChecked():
         reply = QtWidgets.QMessageBox.warning(self, self.tr('Are you absolutely sure?!?'), \
         self.tr('Are you absolutely sure you want to permanently delete '
         'this wallet?  Unless this wallet is saved on another device '
         'you will permanently lose access to all the addresses in this '
         'wallet.'), QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.Cancel)
      elif self.radioWatch.isChecked():
         reply = QtWidgets.QMessageBox.warning(self, self.tr('Are you absolutely sure?!?'), \
         self.tr('<i>This will permanently delete the information you need to spend '
         'funds from this wallet!</i>  You will only be able to receive '
         'coins, but not spend them.  Only do this if you have another copy '
         'of this wallet elsewhere, such as a paper backup or on an offline '
         'computer with the full wallet.'), QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.Cancel)

      if reply == QtWidgets.QMessageBox.Yes:

         thepath = wlt.getWalletPath()
         thepathBackup = wlt.getWalletPath('backup')

         if self.radioWatch.isChecked():
            LOGINFO('***Converting to watching-only wallet')
            newWltPath = wlt.getWalletPath('WatchOnly')
            wlt.forkOnlineWallet(newWltPath, wlt.labelName, wlt.labelDescr)
            self.main.removeWalletFromApplication(wltID)

            newWlt = PyBtcWallet().readWalletFile(newWltPath)
            self.main.addWalletToApplication(newWlt, True)
            # Removed this line of code because it's part of the old BDM paradigm.
            # Leaving this comment here in case it needs to be replaced by anything
            # newWlt.syncWithBlockchainLite()

            os.remove(thepath)
            os.remove(thepathBackup)
            self.main.statusBar().showMessage( \
               self.tr('Wallet %s was replaced with a watching-only wallet.' % wltID), 10000)

         elif self.radioDelete.isChecked():
            LOGINFO('***Completely deleting wallet')
            if not wlt.delete():
               LOGERROR("failed to delete wallet")
               raise Exception("failed to delete wallet")

            self.main.removeWalletFromApplication(wltID)
            self.main.statusBar().showMessage( \
               self.tr('Wallet %s was deleted!' % wltID), 10000)

         self.parent.accept()
         self.accept()
      else:
         self.reject()


################################################################################
class DlgRemoveAddress(ArmoryDialog):
   def __init__(self, wlt, addr160, parent=None, main=None):
      super(DlgRemoveAddress, self).__init__(parent, main)


      if not wlt.hasScrAddr(addr160):
         raise WalletAddressError('Address does not exist in wallet!')

      addrIndex = wlt.cppWallet.getAssetIndexForAddr(addr160)
      self.cppAddrObj = wlt.cppWallet.getAddrObjByIndex(addrIndex)

      if addrIndex >= 0:
         raise WalletAddressError('Cannot delete regular chained addresses! '
                                   'Can only delete imported addresses.')


      importIndex = wlt.cppWallet.convertToImportIndex(addrIndex)

      self.wlt = wlt
      importStr = wlt.linearAddr160List[importIndex]
      self.addr = wlt.addrMap[importStr]
      self.comm = wlt.getCommentForAddress(addr160)

      lblWarning = QtWidgets.QLabel(self.tr('<b>!!! WARNING !!!</b>\n\n'))
      lblWarning.setTextFormat(QtCore.Qt.RichText)
      lblWarning.setAlignment(QtCore.Qt.AlignHCenter)

      lblWarning2 = QtWidgets.QLabel(self.tr('<i>You have requested that the following address '
                            'be deleted from your wallet:</i>'))
      lblWarning.setTextFormat(QtCore.Qt.RichText)
      lblWarning.setWordWrap(True)
      lblWarning.setAlignment(QtCore.Qt.AlignHCenter)

      lbls = []
      lbls.append([])
      lbls[-1].append(QtWidgets.QLabel(self.tr('Address:')))
      lbls[-1].append(QtWidgets.QLabel(self.cppAddrObj.getScrAddr()))
      lbls.append([])
      lbls[-1].append(QtWidgets.QLabel(self.tr('Comment:')))
      lbls[-1].append(QtWidgets.QLabel(self.comm))
      lbls[-1][-1].setWordWrap(True)
      lbls.append([])
      lbls[-1].append(QtWidgets.QLabel(self.tr('In Wallet:')))
      lbls[-1].append(QtWidgets.QLabel('"%s" (%s)' % (wlt.labelName, wlt.uniqueIDB58)))

      addrEmpty = True
      if TheBDM.getState() == BDM_BLOCKCHAIN_READY:
         bal = wlt.getAddrBalance(addr160, 'Full')
         lbls.append([])
         lbls[-1].append(QtWidgets.QLabel(self.tr('Address Balance (w/ unconfirmed):')))
         if bal > 0:
            lbls[-1].append(QtWidgets.QLabel('<font color="red"><b>' + coin2str(bal, maxZeros=1) + ' BTC</b></font>'))
            lbls[-1][-1].setTextFormat(QtCore.Qt.RichText)
            addrEmpty = False
         else:
            lbls[3].append(QtWidgets.QLabel(coin2str(bal, maxZeros=1) + ' BTC'))


      # Add two WARNING images on either side of dialog
      lblWarnImg = QtWidgets.QLabel()
      lblWarnImg.setPixmap(QtGui.QPixmap('./img/MsgBox_warning48.png'))
      lblWarnImg.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
      lblWarnImg2 = QtWidgets.QLabel()
      lblWarnImg2.setPixmap(QtGui.QPixmap('./img/MsgBox_warning48.png'))
      lblWarnImg2.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)

      # Add the warning text and images to the top of the dialog
      layout = QtWidgets.QGridLayout()
      layout.addWidget(lblWarning, 0, 1, 1, 1)
      layout.addWidget(lblWarning2, 1, 1, 1, 1)
      layout.addWidget(lblWarnImg, 0, 0, 2, 1)
      layout.addWidget(lblWarnImg2, 0, 2, 2, 1)

      frmInfo = QtWidgets.QFrame()
      frmInfo.setFrameStyle(QtWidgets.QFrame.Box | QtWidgets.QFrame.Plain)
      frmInfo.setLineWidth(3)
      frmInfoLayout = QtWidgets.QGridLayout()
      for i in range(len(lbls)):
         lbls[i][0].setText('<b>' + lbls[i][0].text() + '</b>')
         lbls[i][0].setTextFormat(QtCore.Qt.RichText)
         frmInfoLayout.addWidget(lbls[i][0], i, 0)
         frmInfoLayout.addWidget(lbls[i][1], i, 1, 1, 2)

      frmInfo.setLayout(frmInfoLayout)
      layout.addWidget(frmInfo, 2, 0, 2, 3)

      lblDelete = QtWidgets.QLabel(\
            self.tr('Do you want to delete this address?  No other addresses in this '
            'wallet will be affected.'))
      lblDelete.setWordWrap(True)
      lblDelete.setTextFormat(QtCore.Qt.RichText)
      layout.addWidget(lblDelete, 4, 0, 1, 3)

      bbox = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | \
                              QtWidgets.QDialogButtonBox.Cancel)
      self.connect(bbox, SIGNAL('accepted()'), self.removeAddress)
      self.connect(bbox, SIGNAL('rejected()'), self.reject)
      layout.addWidget(bbox, 5, 0, 1, 3)

      self.setLayout(layout)
      self.setWindowTitle(self.tr('Confirm Delete Address'))


   def removeAddress(self):
      reply = QtWidgets.QMessageBox.warning(self, self.tr('One more time...'), self.tr(
           'Simply deleting an address does not prevent anyone '
           'from sending money to it.  If you have given this address '
           'to anyone in the past, make sure that they know not to '
           'use it again, since any bitcoins sent to it will be '
           'inaccessible.\n\n '
           'If you are maintaining an external copy of this address '
           'please ignore this warning\n\n'
           'Are you absolutely sure you want to delete %s ?' % self.cppAddrObj.getScrAddr()) , \
           QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.Cancel)

      if reply == QtWidgets.QMessageBox.Yes:
         self.wlt.deleteImportedAddress(self.addr.getAddr160())
         try:
            self.parent.wltAddrModel.reset()
            self.parent.setSummaryBalances()
         except AttributeError:
            pass
         self.accept()

      else:
         self.reject()

################################################################################
class DlgAddressProperties(ArmoryDialog):
   def __init__(self, wlt, parent=None, main=None):
      super(DlgAddressProperties, self).__init__(parent, main)

class GfxViewPaper(QtWidgets.QGraphicsView):
   def __init__(self, parent=None, main=None):
      super(GfxViewPaper, self).__init__(parent)
      self.setRenderHint(QtGui.QPainter.TextAntialiasing)

class GfxItemText(QtWidgets.QGraphicsTextItem):
   """
   So far, I'm pretty bad at setting the boundingRect properly.  I have
   hacked it to be usable for this specific situation, but it's not very
   reusable...
   """
   def __init__(self, text, position, scene, font=GETFONT('Courier', 8), lineWidth=None):
      super(GfxItemText, self).__init__(text)
      self.setFont(font)
      self.setPos(position)
      if lineWidth:
         self.setTextWidth(lineWidth)

      self.setDefaultTextColor(self.PAGE_TEXT_COLOR)

   def boundingRect(self):
      w, h = relaxedSizeStr(self, self.toPlainText())
      nLine = 1
      if self.textWidth() > 0:
         twid = self.textWidth()
         nLine = max(1, int(float(w) / float(twid) + 0.5))
      return QtCore.QRectF(0, 0, w, nLine * (1.5 * h))


################################################################################
class DlgBadConnection(ArmoryDialog):
   def __init__(self, haveInternet, haveSatoshi, parent=None, main=None):
      super(DlgBadConnection, self).__init__(parent, main)


      layout = QtWidgets.QGridLayout()
      lblWarnImg = QtWidgets.QLabel()
      lblWarnImg.setPixmap(QtGui.QPixmap('./img/MsgBox_warning48.png'))
      lblWarnImg.setAlignment(QtCore.Qt.AlignHCenter | QtCore.Qt.AlignTop)

      lblDescr = QtWidgets.QLabel()
      if not haveInternet and not CLI_OPTIONS.offline:
         lblDescr = QRichLabel(self.tr(
            'Armory was not able to detect an internet connection, so Armory '
            'will operate in "Offline" mode.  In this mode, only wallet '
            '-management and unsigned-transaction functionality will be available. '
            '<br><br>'
            'If this is an error, please check your internet connection and '
            'restart Armory.<br><br>Would you like to continue in "Offline" mode?'))
      elif haveInternet and not haveSatoshi:
         lblDescr = QRichLabel(self.tr(
            'Armory was not able to detect the presence of Bitcoin Core or bitcoind '
            'client software (available at https://bitcoin.org).  Please make sure that '
            'the one of those programs is... <br> '
            '<br><b>(1)</b> ...open and connected to the network '
            '<br><b>(2)</b> ...on the same network as Armory (main-network or test-network) '
            '<br><b>(3)</b> ...synchronized with the blockchain before '
            'starting Armory<br><br>Without the Bitcoin Core or bitcoind open, you will only '
            'be able to run Armory in "Offline" mode, which will not have access '
            'to new blockchain data, and you will not be able to send outgoing '
            'transactions<br><br>If you do not want to be in "Offline" mode, please '
            'restart Armory after one of these programs is open and synchronized with '
            'the network'))
      else:
         # Nothing to do -- we shouldn't have even gotten here
         # self.reject()
         pass


      self.main.abortLoad = False
      def abortLoad():
         self.main.abortLoad = True
         self.reject()

      lblDescr.setMinimumWidth(500)
      self.btnAccept = QtWidgets.QPushButton(self.tr("Continue in Offline Mode"))
      self.btnCancel = QtWidgets.QPushButton(self.tr("Close Armory"))
      self.connect(self.btnAccept, SIGNAL(CLICKED), self.accept)
      self.connect(self.btnCancel, SIGNAL(CLICKED), abortLoad)
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnAccept, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)

      layout.addWidget(lblWarnImg, 0, 1, 2, 1)
      layout.addWidget(lblDescr, 0, 2, 1, 1)
      layout.addWidget(buttonBox, 1, 2, 1, 1)

      self.setLayout(layout)
      self.setWindowTitle(self.tr('Network not available'))


################################################################################
def readSigBlock(parent, fullPacket):
   addrB58, messageStr, pubkey, sig = '', '', '', ''
   lines = fullPacket.split('\n')
   readingMessage, readingPub, readingSig = False, False, False
   for i in range(len(lines)):
      s = lines[i].strip()

      # ADDRESS
      if s.startswith('Addr'):
         addrB58 = s.split(':')[-1].strip()

      # MESSAGE STRING
      if s.startswith('Message') or readingMessage:
         readingMessage = True
         if s.startswith('Pub') or s.startswith('Sig') or ('END-CHAL' in s):
            readingMessage = False
         else:
            # Message string needs to be exact, grab what's between the
            # double quotes, no newlines
            iq1 = s.index('"') + 1
            iq2 = s.index('"', iq1)
            messageStr += s[iq1:iq2]

      # PUBLIC KEY
      if s.startswith('Pub') or readingPub:
         readingPub = True
         if s.startswith('Sig') or ('END-SIGNATURE-BLOCK' in s):
            readingPub = False
         else:
            pubkey += s.split(':')[-1].strip().replace(' ', '')

      # SIGNATURE
      if s.startswith('Sig') or readingSig:
         readingSig = True
         if 'END-SIGNATURE-BLOCK' in s:
            readingSig = False
         else:
            sig += s.split(':')[-1].strip().replace(' ', '')


   if len(pubkey) > 0:
      try:
         pubkey = hex_to_binary(pubkey)
         if len(pubkey) not in (32, 33, 64, 65):  raise
      except:
         QtWidgets.QMessageBox.critical(parent, parent.tr('Bad Public Key'), \
             parent.tr('Public key data was not recognized'), QtWidgets.QMessageBox.Ok)
         pubkey = ''

   if len(sig) > 0:
      try:
         sig = hex_to_binary(sig)
      except:
         QtWidgets.QMessageBox.critical(parent, parent.tr('Bad Signature'), \
            parent.tr('Signature data is malformed!'), QtWidgets.QMessageBox.Ok)
         sig = ''


   pubkeyhash = hash160(pubkey)
   if not pubkeyhash == addrStr_to_hash160(addrB58)[1]:
      QtWidgets.QMessageBox.critical(parent, parent.tr('Address Mismatch'), \
         parent.tr('!!! The address included in the signature block does not '
         'match the supplied public key!  This should never happen, '
         'and may in fact be an attempt to mislead you !!!'), QtWidgets.QMessageBox.Ok)
      sig = ''



   return addrB58, messageStr, pubkey, sig


################################################################################
def makeSigBlock(addrB58, MessageStr, binPubkey='', binSig=''):
   lineWid = 50
   s = '-----BEGIN-SIGNATURE-BLOCK'.ljust(lineWid + 13, '-') + '\n'

   ### Address ###
   s += 'Address:    %s\n' % addrB58

   ### Message ###
   chPerLine = lineWid - 2
   nMessageLines = (len(MessageStr) - 1) / chPerLine + 1
   for i in range(nMessageLines):
      cLine = 'Message:    "%s"\n' if i == 0 else '            "%s"\n'
      s += cLine % MessageStr[i * chPerLine:(i + 1) * chPerLine]

   ### Public Key ###
   if len(binPubkey) > 0:
      hexPub = binary_to_hex(binPubkey)
      nPubLines = (len(hexPub) - 1) / lineWid + 1
      for i in range(nPubLines):
         pLine = 'PublicKey:  %s\n' if i == 0 else '            %s\n'
         s += pLine % hexPub[i * lineWid:(i + 1) * lineWid]

   ### Signature ###
   if len(binSig) > 0:
      hexSig = binary_to_hex(binSig)
      nSigLines = (len(hexSig) - 1) / lineWid + 1
      for i in range(nSigLines):
         sLine = 'Signature:  %s\n' if i == 0 else '            %s\n'
         s += sLine % hexSig[i * lineWid:(i + 1) * lineWid]

   s += '-----END-SIGNATURE-BLOCK'.ljust(lineWid + 13, '-') + '\n'
   return s



class DlgExecLongProcess(ArmoryDialog):
   """
   Execute a processing that may require having the user to wait a while.
   Should appear like a splash screen, and will automatically close when
   the processing is done.  As such, you should have very little text, just
   in case it finishes immediately, the user won't have time to read it.

   DlgExecLongProcess(execFunc, 'Short Description', self, self.main).exec_()
   """
   def __init__(self, funcExec, msg='', parent=None, main=None):
      super(DlgExecLongProcess, self).__init__(parent, main)

      self.func = funcExec

      waitFont = GETFONT('Var', 14)
      descrFont = GETFONT('Var', 12)
      palette = QtGui.QPalette()
      palette.setColor(QtGui.QPalette.Window, QtGui.QColor(235, 235, 255))
      self.setPalette(palette);
      self.setAutoFillBackground(True)

      if parent:
         qr = parent.geometry()
         x, y, w, h = qr.left(), qr.top(), qr.width(), qr.height()
         dlgW = relaxedSizeStr(waitFont, msg)[0]
         dlgW = min(dlgW, 400)
         dlgH = 150
         self.setGeometry(int(x + w / 2 - dlgW / 2), int(y + h / 2 - dlgH / 2), dlgW, dlgH)

      lblWaitMsg = QRichLabel(self.tr('Please Wait...'))
      lblWaitMsg.setFont(waitFont)
      lblWaitMsg.setAlignment(QtCore.Qt.AlignVCenter | QtCore.Qt.AlignHCenter)

      lblDescrMsg = QRichLabel(msg)
      lblDescrMsg.setFont(descrFont)
      lblDescrMsg.setAlignment(QtCore.Qt.AlignVCenter | QtCore.Qt.AlignHCenter)

      self.setWindowFlags(QtCore.Qt.SplashScreen)

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblWaitMsg)
      layout.addWidget(lblDescrMsg)
      self.setLayout(layout)


   def exec_(self):
      def execAndClose():
         self.func()
         self.accept()

      self.callLater(0.1, execAndClose)
      QtWidgets.QDialog.exec_(self)






################################################################################
class DlgECDSACalc(ArmoryDialog):
   def __init__(self, parent=None, main=None, tabStart=0):
      super(DlgECDSACalc, self).__init__(parent, main)

      dispFont = GETFONT('Var', 8)
      w, h = tightSizeNChar(dispFont, 40)



      ##########################################################################
      ##########################################################################
      # TAB:  secp256k1
      ##########################################################################
      ##########################################################################
      # STUB: I'll probably finish implementing this eventually....
      eccWidget = QtWidgets.QWidget()

      tabEccLayout = QtWidgets.QGridLayout()
      self.txtScalarScalarA = QtWidgets.QLineEdit()
      self.txtScalarScalarB = QtWidgets.QLineEdit()
      self.txtScalarScalarC = QtWidgets.QLineEdit()
      self.txtScalarScalarC.setReadOnly(True)

      self.txtScalarPtA = QtWidgets.QLineEdit()
      self.txtScalarPtB_x = QtWidgets.QLineEdit()
      self.txtScalarPtB_y = QtWidgets.QLineEdit()
      self.txtScalarPtC_x = QtWidgets.QLineEdit()
      self.txtScalarPtC_y = QtWidgets.QLineEdit()
      self.txtScalarPtC_x.setReadOnly(True)
      self.txtScalarPtC_y.setReadOnly(True)

      self.txtPtPtA_x = QtWidgets.QLineEdit()
      self.txtPtPtA_y = QtWidgets.QLineEdit()
      self.txtPtPtB_x = QtWidgets.QLineEdit()
      self.txtPtPtB_y = QtWidgets.QLineEdit()
      self.txtPtPtC_x = QtWidgets.QLineEdit()
      self.txtPtPtC_y = QtWidgets.QLineEdit()
      self.txtPtPtC_x.setReadOnly(True)
      self.txtPtPtC_y.setReadOnly(True)

      eccTxtList = [ \
          self.txtScalarScalarA, self.txtScalarScalarB, \
          self.txtScalarScalarC, self.txtScalarPtA, self.txtScalarPtB_x, \
          self.txtScalarPtB_y, self.txtScalarPtC_x, self.txtScalarPtC_y, \
          self.txtPtPtA_x, self.txtPtPtA_y, self.txtPtPtB_x, \
          self.txtPtPtB_y, self.txtPtPtC_x, self.txtPtPtC_y]

      dispFont = GETFONT('Var', 8)
      w, h = tightSizeNChar(dispFont, 60)
      for txt in eccTxtList:
         txt.setMinimumWidth(w)
         txt.setFont(dispFont)


      self.btnCalcSS = QtWidgets.QPushButton(self.tr('Multiply Scalars (mod n)'))
      self.btnCalcSP = QtWidgets.QPushButton(self.tr('Scalar Multiply EC Point'))
      self.btnCalcPP = QtWidgets.QPushButton(self.tr('Add EC Points'))
      self.btnClearSS = QtWidgets.QPushButton(self.tr('Clear'))
      self.btnClearSP = QtWidgets.QPushButton(self.tr('Clear'))
      self.btnClearPP = QtWidgets.QPushButton(self.tr('Clear'))


      imgPlus = QRichLabel('<b>+</b>')
      imgTimes1 = QRichLabel('<b>*</b>')
      imgTimes2 = QRichLabel('<b>*</b>')
      imgDown = QRichLabel('')

      self.connect(self.btnCalcSS, SIGNAL(CLICKED), self.multss)
      self.connect(self.btnCalcSP, SIGNAL(CLICKED), self.multsp)
      self.connect(self.btnCalcPP, SIGNAL(CLICKED), self.addpp)


      ##########################################################################
      # Scalar-Scalar Multiply
      sslblA = QRichLabel('a', hAlign=QtCore.Qt.AlignHCenter)
      sslblB = QRichLabel('b', hAlign=QtCore.Qt.AlignHCenter)
      sslblC = QRichLabel('a*b mod n', hAlign=QtCore.Qt.AlignHCenter)


      ssLayout = QtWidgets.QGridLayout()
      ssLayout.addWidget(sslblA, 0, 0, 1, 1)
      ssLayout.addWidget(sslblB, 0, 2, 1, 1)

      ssLayout.addWidget(self.txtScalarScalarA, 1, 0, 1, 1)
      ssLayout.addWidget(imgTimes1, 1, 1, 1, 1)
      ssLayout.addWidget(self.txtScalarScalarB, 1, 2, 1, 1)

      ssLayout.addWidget(makeHorizFrame([STRETCH, self.btnCalcSS, STRETCH]), \
                                                  2, 0, 1, 3)
      ssLayout.addWidget(makeHorizFrame([STRETCH, sslblC, self.txtScalarScalarC, STRETCH]), \
                                                  3, 0, 1, 3)
      ssLayout.setVerticalSpacing(1)
      frmSS = QtWidgets.QFrame()
      frmSS.setFrameStyle(STYLE_SUNKEN)
      frmSS.setLayout(ssLayout)

      ##########################################################################
      # Scalar-ECPoint Multiply
      splblA = QRichLabel('a', hAlign=QtCore.Qt.AlignHCenter)
      splblB = QRichLabel('<b>B</b>', hAlign=QtCore.Qt.AlignHCenter)
      splblBx = QRichLabel('<b>B</b><font size=2>x</font>', hAlign=QtCore.Qt.AlignRight)
      splblBy = QRichLabel('<b>B</b><font size=2>y</font>', hAlign=QtCore.Qt.AlignRight)
      splblC = QRichLabel('<b>C</b> = a*<b>B</b>', hAlign=QtCore.Qt.AlignHCenter)
      splblCx = QRichLabel('(a*<b>B</b>)<font size=2>x</font>', hAlign=QtCore.Qt.AlignRight)
      splblCy = QRichLabel('(a*<b>B</b>)<font size=2>y</font>', hAlign=QtCore.Qt.AlignRight)
      spLayout = QtWidgets.QGridLayout()
      spLayout.addWidget(splblA, 0, 0, 1, 1)
      spLayout.addWidget(splblB, 0, 2, 1, 1)

      spLayout.addWidget(self.txtScalarPtA, 1, 0, 1, 1)
      spLayout.addWidget(imgTimes2, 1, 1, 1, 1)
      spLayout.addWidget(self.txtScalarPtB_x, 1, 2, 1, 1)
      spLayout.addWidget(self.txtScalarPtB_y, 2, 2, 1, 1)

      spLayout.addWidget(makeHorizFrame([STRETCH, self.btnCalcSP, STRETCH]), \
                                                  3, 0, 1, 3)
      spLayout.addWidget(makeHorizFrame([STRETCH, splblCx, self.txtScalarPtC_x, STRETCH]), \
                                                  4, 0, 1, 3)
      spLayout.addWidget(makeHorizFrame([STRETCH, splblCy, self.txtScalarPtC_y, STRETCH]), \
                                                  5, 0, 1, 3)
      spLayout.setVerticalSpacing(1)
      frmSP = QtWidgets.QFrame()
      frmSP.setFrameStyle(STYLE_SUNKEN)
      frmSP.setLayout(spLayout)

      ##########################################################################
      # ECPoint Addition
      pplblA = QRichLabel('<b>A</b>', hAlign=QtCore.Qt.AlignHCenter)
      pplblB = QRichLabel('<b>B</b>', hAlign=QtCore.Qt.AlignHCenter)
      pplblAx = QRichLabel('<b>A</b><font size=2>x</font>', hAlign=QtCore.Qt.AlignHCenter)
      pplblAy = QRichLabel('<b>A</b><font size=2>y</font>', hAlign=QtCore.Qt.AlignHCenter)
      pplblBx = QRichLabel('<b>B</b><font size=2>x</font>', hAlign=QtCore.Qt.AlignHCenter)
      pplblBy = QRichLabel('<b>B</b><font size=2>y</font>', hAlign=QtCore.Qt.AlignHCenter)
      pplblC = QRichLabel('<b>C</b> = <b>A</b>+<b>B</b>', hAlign=QtCore.Qt.AlignHCenter)
      pplblCx = QRichLabel('(<b>A</b>+<b>B</b>)<font size=2>x</font>', hAlign=QtCore.Qt.AlignRight)
      pplblCy = QRichLabel('(<b>A</b>+<b>B</b>)<font size=2>y</font>', hAlign=QtCore.Qt.AlignRight)
      ppLayout = QtWidgets.QGridLayout()
      ppLayout.addWidget(pplblA, 0, 0, 1, 1)
      ppLayout.addWidget(pplblB, 0, 2, 1, 1)
      ppLayout.addWidget(self.txtPtPtA_x, 1, 0, 1, 1)
      ppLayout.addWidget(self.txtPtPtA_y, 2, 0, 1, 1)
      ppLayout.addWidget(imgPlus, 1, 1, 2, 1)
      ppLayout.addWidget(self.txtPtPtB_x, 1, 2, 1, 1)
      ppLayout.addWidget(self.txtPtPtB_y, 2, 2, 1, 1)
      ppLayout.addWidget(makeHorizFrame([STRETCH, self.btnCalcPP, STRETCH]), \
                                                  3, 0, 1, 3)
      ppLayout.addWidget(makeHorizFrame([STRETCH, pplblCx, self.txtPtPtC_x, STRETCH]), \
                                                  4, 0, 1, 3)
      ppLayout.addWidget(makeHorizFrame([STRETCH, pplblCy, self.txtPtPtC_y, STRETCH]), \
                                                  5, 0, 1, 3)
      ppLayout.setVerticalSpacing(1)
      frmPP = QtWidgets.QFrame()
      frmPP.setFrameStyle(STYLE_SUNKEN)
      frmPP.setLayout(ppLayout)

      gxstr = '79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798'
      gystr = '483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8'

      lblDescr = QRichLabel(self.tr(
         'Use this form to perform Bitcoin elliptic curve calculations.  All '
         'operations are performed on the secp256k1 elliptic curve, which is '
         'the one used for Bitcoin. '
         'Supply all values as 32-byte, big-endian, hex-encoded integers. '
         '<br><br>'
         'The following is the secp256k1 generator point coordinates (G): <br> '
         '<b>G</b><sub>x</sub>: %s <br> '
         '<b>G</b><sub>y</sub>: %s' % (gxstr, gystr)))

      lblDescr.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse | \
                                       QtCore.Qt.TextSelectableByKeyboard)

      btnClear = QtWidgets.QPushButton(self.tr('Clear'))
      btnClear.setMaximumWidth(2 * relaxedSizeStr(btnClear, 'Clear')[0])
      self.connect(btnClear, SIGNAL(CLICKED), self.eccClear)

      btnBack = QtWidgets.QPushButton(self.tr('<<< Go Back'))
      self.connect(btnBack, SIGNAL(CLICKED), self.accept)
      frmBack = makeHorizFrame([btnBack, STRETCH])

      eccLayout = QtWidgets.QVBoxLayout()
      eccLayout.addWidget(makeHorizFrame([lblDescr, btnClear]))
      eccLayout.addWidget(frmSS)
      eccLayout.addWidget(frmSP)
      eccLayout.addWidget(frmBack)

      eccWidget.setLayout(eccLayout)

      calcLayout = QtWidgets.QHBoxLayout()
      calcLayout.addWidget(eccWidget)
      self.setLayout(calcLayout)

      self.setWindowTitle(self.tr('ECDSA Calculator'))
      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))


   #############################################################################
   def getBinary(self, widget, name):
      try:
         hexVal = str(widget.text())
         binVal = hex_to_binary(hexVal)
      except:
         QtWidgets.QMessageBox.critical(self, self.tr('Bad Input'), self.tr('Value "%s" is invalid. '
            'Make sure the value is specified in hex, big-endian.' % name) , QtWidgets.QMessageBox.Ok)
         return ''
      return binVal


   #############################################################################
   def multss(self):
      binA = self.getBinary(self.txtScalarScalarA, 'a')
      binB = self.getBinary(self.txtScalarScalarB, 'b')
      C = CryptoECDSA().ECMultiplyScalars(binA, binB)
      self.txtScalarScalarC.setText(binary_to_hex(C))

      for txt in [self.txtScalarScalarA, \
                  self.txtScalarScalarB, \
                  self.txtScalarScalarC]:
         txt.setCursorPosition(0)

   #############################################################################
   def multsp(self):
      binA = self.getBinary(self.txtScalarPtA, 'a')
      binBx = self.getBinary(self.txtScalarPtB_x, '<b>B</b><font size=2>x</font>')
      binBy = self.getBinary(self.txtScalarPtB_y, '<b>B</b><font size=2>y</font>')

      if not CryptoECDSA().ECVerifyPoint(binBx, binBy):
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid EC Point'), \
            self.tr('The point you specified (<b>B</b>) is not on the '
            'elliptic curve used in Bitcoin (secp256k1).'), QtWidgets.QMessageBox.Ok)
         return

      C = CryptoECDSA().ECMultiplyPoint(binA, binBx, binBy)
      self.txtScalarPtC_x.setText(binary_to_hex(C[:32]))
      self.txtScalarPtC_y.setText(binary_to_hex(C[32:]))

      for txt in [self.txtScalarPtA, \
                  self.txtScalarPtB_x, self.txtScalarPtB_y, \
                  self.txtScalarPtC_x, self.txtScalarPtC_y]:
         txt.setCursorPosition(0)

   #############################################################################
   def addpp(self):
      binAx = self.getBinary(self.txtPtPtA_x, '<b>A</b><font size=2>x</font>')
      binAy = self.getBinary(self.txtPtPtA_y, '<b>A</b><font size=2>y</font>')
      binBx = self.getBinary(self.txtPtPtB_x, '<b>B</b><font size=2>x</font>')
      binBy = self.getBinary(self.txtPtPtB_y, '<b>B</b><font size=2>y</font>')

      if not CryptoECDSA().ECVerifyPoint(binAx, binAy):
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid EC Point'), \
            self.tr('The point you specified (<b>A</b>) is not on the '
            'elliptic curve used in Bitcoin (secp256k1).'), QtWidgets.QMessageBox.Ok)
         return

      if not CryptoECDSA().ECVerifyPoint(binBx, binBy):
         QtWidgets.QMessageBox.critical(self, self.tr('Invalid EC Point'), \
            self.tr('The point you specified (<b>B</b>) is not on the '
            'elliptic curve used in Bitcoin (secp256k1).'), QtWidgets.QMessageBox.Ok)
         return

      C = CryptoECDSA().ECAddPoints(binAx, binAy, binBx, binBy)
      self.txtPtPtC_x.setText(binary_to_hex(C[:32]))
      self.txtPtPtC_y.setText(binary_to_hex(C[32:]))

      for txt in [self.txtPtPtA_x, self.txtPtPtA_y, \
                  self.txtPtPtB_x, self.txtPtPtB_y, \
                  self.txtPtPtC_x, self.txtPtPtC_y]:
         txt.setCursorPosition(0)


   #############################################################################
   def eccClear(self):
      self.txtScalarScalarA.setText('')
      self.txtScalarScalarB.setText('')
      self.txtScalarScalarC.setText('')

      self.txtScalarPtA.setText('')
      self.txtScalarPtB_x.setText('')
      self.txtScalarPtB_y.setText('')
      self.txtScalarPtC_x.setText('')
      self.txtScalarPtC_y.setText('')

      self.txtPtPtA_x.setText('')
      self.txtPtPtA_y.setText('')
      self.txtPtPtB_x.setText('')
      self.txtPtPtB_y.setText('')
      self.txtPtPtC_x.setText('')
      self.txtPtPtC_y.setText('')


################################################################################
# STUB STUB STUB STUB STUB
class ArmoryPref(object):
   """
   Create a class that will handle arbitrary preferences for Armory.  This
   means that I can just create maps/lists of preferences, and auto-include
   them in the preferences dialog, and know how to set/get them.  This will
   be subclassed for each unique/custom preference type that is needed.
   """
   def __init__(self, prefName, dispStr, setType, defaultVal, validRange, descr, ttip, usermodes=None):
      self.preference = prefName
      self.displayStr = dispStr
      self.preferType = setType
      self.defaultVal = defaultVal
      self.validRange = validRange
      self.description = descr
      self.ttip = ttip

      # Some options may only be displayed for certain usermodes
      self.users = usermodes
      if usermodes == None:
         self.users = set([USERMODE.Standard, USERMODE.Advanced, USERMODE.Expert])

      if self.preferType == 'str':
         self.entryObj = QtWidgets.QLineEdit()
      elif self.preferType == 'num':
         self.entryObj = QtWidgets.QLineEdit()
      elif self.preferType == 'file':
         self.entryObj = QtWidgets.QLineEdit()
      elif self.preferType == 'bool':
         self.entryObj = QtWidgets.QCheckBox()
      elif self.preferType == 'combo':
         self.entryObj = QtWidgets.QComboBox()


   def setEntryVal(self):
      pass

   def readEntryVal(self):
      pass


   def setWidthChars(self, nChar):
      self.entryObj.setMinimumWidth(relaxedSizeNChar(self.entryObj, nChar)[0])

   def render(self):
      """
      Return a map of qt objects to insert into the frame
      """
      toDraw = []
      row = 0
      if len(self.description) > 0:
         toDraw.append([QRichLabel(self.description), row, 0, 1, 4])
         row += 1


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
# Class that acts as a center where the user can decide what to do with the
# watch-only wallet. The data can be displayed, printed, or saved to a file as a
# wallet or as the watch-only data (i.e., root key & chain code).
class DlgExpWOWltData(ArmoryDialog):
   """
   This dialog will be used to export a wallet's root public key and chain code.
   """
   def __init__(self, wlt, parent, main):
      super(DlgExpWOWltData, self).__init__(parent, main)

      # Save a copy of the wallet.
      self.wlt = wlt
      self.main = main

      # Get the chain code and uncompressed public key of info from the wallet,
      # along with other useful info.
      wltRootIDConcat, pkccET16Lines = wlt.getRootPKCCBackupData(True)
      wltIDB58 = wlt.uniqueIDB58

      # Create the data export buttons.
      expWltButton = QtWidgets.QPushButton(self.tr('Export Watching-Only Wallet File'))
      clipboardBtn = QtWidgets.QPushButton(self.tr('Copy to clipboard'))
      clipboardLbl = QRichLabel('', hAlign=QtCore.Qt.AlignHCenter)
      expDataButton = QtWidgets.QPushButton(self.tr('Save to Text File'))
      printWODataButton = QtWidgets.QPushButton(self.tr('Print Root Data'))


      self.connect(expWltButton, SIGNAL(CLICKED), self.clickedExpWlt)
      self.connect(expDataButton, SIGNAL(CLICKED), self.clickedExpData)
      self.connect(printWODataButton, SIGNAL(CLICKED), \
                   self.clickedPrintWOData)


      # Let's put the window together.
      layout = QtWidgets.QVBoxLayout()

      self.dispText = self.tr(
         'Watch-Only Root ID:<br><b>%s</b>'
         '<br><br>'
         'Watch-Only Root Data:' % wltRootIDConcat)
      for j in pkccET16Lines:
         self.dispText += '<br><b>%s</b>' % (j)

      titleStr = self.tr('Watch-Only Wallet Export')

      self.txtLongDescr = QtWidgets.QTextBrowser()
      self.txtLongDescr.setFont(GETFONT('Fixed', 9))
      self.txtLongDescr.setHtml(self.dispText)
      w,h = tightSizeNChar(self.txtLongDescr, 20)
      self.txtLongDescr.setMaximumHeight(9.5*h)

      def clippy():
         clipb = QtWidgets.QApplication.clipboard()
         clipb.clear()
         clipb.setText(str(self.txtLongDescr.toPlainText()))
         clipboardLbl.setText(self.tr('<i>Copied!</i>'))

      self.connect(clipboardBtn, SIGNAL('clicked()'), clippy)


      lblDescr = QRichLabel(self.tr(
         '<center><b><u><font size=4 color="%s">Export Watch-Only '
         'Wallet: %s</font></u></b></center> '
         '<br>'
         'Use a watching-only wallet on an online computer to distribute '
         'payment addresses, verify transactions and monitor balances, but '
         'without the ability to move the funds.' % (htmlColor('TextBlue'), wlt.uniqueIDB58)))

      lblTopHalf = QRichLabel(self.tr(
         '<center><b><u>Entire Wallet File</u></b></center> '
         '<br>'
         '<i><b><font color="%s">(Recommended)</font></b></i> '
         'An exact copy of your wallet file but without any of the private '
         'signing keys. All existing comments and labels will be carried '
         'with the file. Use this option if it is easy to transfer files '
         'from this system to the target system.' % htmlColor('TextBlue')))

      lblBotHalf = QRichLabel(self.tr(
         '<center><b><u>Only Root Data</u></b></center> '
         '<br>'
         'Same as above, but only five lines of text that are easy to '
         'print, email inline, or copy by hand.  Only produces the '
         'wallet addresses.   No comments or labels are carried with '
         'it.'))

      btnDone = QtWidgets.QPushButton(self.tr('Done'))
      self.connect(btnDone, SIGNAL('clicked()'), self.accept)


      frmButtons = makeVertFrame([clipboardBtn,
                                  expDataButton,
                                  printWODataButton,
                                  clipboardLbl,
                                  'Stretch'])
      layoutBottom = QtWidgets.QHBoxLayout()
      layoutBottom.addWidget(frmButtons, 0)
      layoutBottom.addItem(QtWidgets.QSpacerItem(5,5))
      layoutBottom.addWidget(self.txtLongDescr, 1)
      layoutBottom.setSpacing(5)


      layout.addWidget(lblDescr)
      layout.addItem(QtWidgets.QSpacerItem(10, 10))
      layout.addWidget(HLINE())
      layout.addWidget(lblTopHalf, 1)
      layout.addWidget(makeHorizFrame(['Stretch', expWltButton, 'Stretch']))
      layout.addItem(QtWidgets.QSpacerItem(20, 20))
      layout.addWidget(HLINE())
      layout.addWidget(lblBotHalf, 1)
      layout.addLayout(layoutBottom)
      layout.addItem(QtWidgets.QSpacerItem(20, 20))
      layout.addWidget(HLINE())
      layout.addWidget(makeHorizFrame(['Stretch', btnDone]))
      layout.setSpacing(3)

      self.setLayout(layout)
      self.setMinimumWidth(600)

      # TODO:  Dear god this is terrible, but for my life I cannot figure
      #        out how to move the vbar, because you can't do it until
      #        the dialog is drawn which doesn't happen til after __init__.
      self.callLater(0.05, self.resizeEvent)

      self.setWindowTitle(titleStr)


   def resizeEvent(self, ev=None):
      super(DlgExpWOWltData, self).resizeEvent(ev)
      vbar = self.txtLongDescr.verticalScrollBar()
      vbar.setValue(vbar.minimum())


   # The function that is executed when the user wants to back up the full
   # watch-only wallet to a file.
   def clickedExpWlt(self):
      currPath = self.wlt.walletPath
      if not self.wlt.watchingOnly:
         pieces = os.path.splitext(currPath)
         currPath = pieces[0] + '_WatchOnly' + pieces[1]

      saveLoc = self.main.getFileSave('Save Watching-Only Copy', \
                                      defaultFilename=currPath)
      if not saveLoc.endswith('.wallet'):
         saveLoc += '.wallet'

      if not self.wlt.watchingOnly:
         self.wlt.forkOnlineWallet(saveLoc, self.wlt.labelName, \
                                '(Watching-Only) ' + self.wlt.labelDescr)
      else:
         self.wlt.writeFreshWalletFile(saveLoc)



   # The function that is executed when the user wants to save the watch-only
   # data to a file.
   def clickedExpData(self):
      self.main.makeWalletCopy(self, self.wlt, 'PKCC', 'rootpubkey')


   # The function that is executed when the user wants to print the watch-only
   # data.
   def clickedPrintWOData(self):
      self.result = DlgWODataPrintBackup(self, self.main, self.wlt).exec_()


################################################################################
# Class that handles the printing of the watch-only wallet data. The formatting
# is mostly the same as a normal paper backup. Note that neither fragmented
# backups nor SecurePrint are used.
class DlgWODataPrintBackup(ArmoryDialog):
   """
   Open up a "Make Paper Backup" dialog, so the user can print out a hard
   copy of whatever data they need to recover their wallet should they lose
   it.
   """
   def __init__(self, parent, main, wlt):
      super(DlgWODataPrintBackup, self).__init__(parent, main)

      self.wlt = wlt

      # Create the scene and the view.
      self.scene = SimplePrintableGraphicsScene(self, self.main)
      self.view = QtWidgets.QGraphicsView()
      self.view.setRenderHint(QtGui.QPainter.TextAntialiasing)
      self.view.setScene(self.scene.getScene())

      # Label displayed above the sheet to be printed.
      lblDescr = QRichLabel(self.tr(
         '<b><u>Print Watch-Only Wallet Root</u></b><br><br> '
         'The lines below are sufficient to calculate public keys '
         'for every private key ever produced by the full wallet. '
         'Importing this data to an online computer is sufficient '
         'to receive and verify transactions, and monitor balances, '
         'but without the ability to spend the funds.'))
      lblDescr.setContentsMargins(5, 5, 5, 5)
      frmDescr = makeHorizFrame([lblDescr], STYLE_RAISED)

      # Buttons shown below the sheet to be printed.
      self.btnPrint = QtWidgets.QPushButton('&Print...')
      self.btnPrint.setMinimumWidth(3 * tightSizeStr(self.btnPrint, 'Print...')[0])
      self.btnCancel = QtWidgets.QPushButton('&Cancel')
      self.connect(self.btnPrint, SIGNAL(CLICKED), self.print_)
      self.connect(self.btnCancel, SIGNAL(CLICKED), self.reject)
      frmButtons = makeHorizFrame([self.btnCancel, STRETCH, self.btnPrint])

      # Draw the sheet for the first time.
      self.redrawBackup()

      # Lay out the dialog.
      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(frmDescr)
      layout.addWidget(self.view)
      layout.addWidget(frmButtons)
      setLayoutStretch(layout, 0, 1, 0)
      self.setLayout(layout)
      self.setWindowIcon(QtGui.QIcon('./img/printer_icon.png'))
      self.setWindowTitle('Print Watch-Only Root')

      # Apparently I can't programmatically scroll until after it's painted
      def scrollTop():
         vbar = self.view.verticalScrollBar()
         vbar.setValue(vbar.minimum())
      self.callLater(0.01, scrollTop)


   # Class called to redraw the print "canvas" when the data changes.
   def redrawBackup(self):
      self.createPrintScene()
      self.view.update()


   # Class that handles the actual printing code.
   def print_(self):
      LOGINFO('Printing!')
      self.printer = QPrinter(QPrinter.HighResolution)
      self.printer.setPageSize(QPrinter.Letter)

      if QPrintDialog(self.printer).exec_():
         painter = QtGui.QPainter(self.printer)
         painter.setRenderHint(QtGui.QPainter.TextAntialiasing)

         self.createPrintScene()
         self.scene.getScene().render(painter)
         painter.end()
         self.accept()


   # Class that lays out the actual print "canvas" to be printed.
   def createPrintScene(self):
      # Do initial setup.
      self.scene.gfxScene.clear()
      self.scene.resetCursor()

      # Draw the background paper?
      pr = self.scene.pageRect()
      self.scene.drawRect(pr.width(), pr.height(), edgeColor=None, \
                          fillColor=QtGui.QColor(255, 255, 255))
      self.scene.resetCursor()

      INCH = self.scene.INCH
      MARGIN = self.scene.MARGIN_PIXELS
      wrap = 0.9 * self.scene.pageRect().width()

      # Start drawing the page.
      if USE_TESTNET or USE_REGTEST:
         self.scene.drawPixmapFile('./img/armory_logo_green_h56.png')
      else:
         self.scene.drawPixmapFile('./img/armory_logo_h36.png')
      self.scene.newLine()

      warnMsg = self.tr(
         '<b><font size=4><font color="#aa0000">WARNING:</font>  <u>This is not '
         'a wallet backup!</u></font></b> '
         '<br><br>Please make a regular digital or paper backup of your wallet '
         'to keep it protected!  This data simply lets you '
         'monitor the funds in this wallet but gives you no ability to move any '
         'funds.')
      self.scene.drawText(warnMsg, GETFONT('Var', 9), wrapWidth=wrap)

      self.scene.newLine(extra_dy=20)
      self.scene.drawHLine()
      self.scene.newLine(extra_dy=20)

      # Print the wallet info.
      colRect, rowHgt = self.scene.drawColumn(['<b>Watch-Only Root Data</b>',
                                               'Wallet ID:',
                                               'Wallet Name:'])
      self.scene.moveCursor(15, 0)
      colRect, rowHgt = self.scene.drawColumn(['',
                                               self.wlt.uniqueIDB58,
                                               self.wlt.labelName])

      self.scene.moveCursor(15, colRect.y() + colRect.height(), absolute=True)

      # Display warning about unprotected key data.
      self.scene.newLine(extra_dy=20)
      self.scene.drawHLine()
      self.scene.newLine(extra_dy=20)

      # Draw the description of the data.
      descrMsg = self.tr(
         'The following five lines are sufficient to reproduce all public '
         'keys matching the private keys produced by the full wallet.')
      self.scene.drawText(descrMsg, GETFONT('var', 8), wrapWidth=wrap)
      self.scene.newLine(extra_dy=10)

      # Prepare the data.
      self.wltRootIDConcat, self.pkccET16Lines = \
                                            self.wlt.getRootPKCCBackupData(True)
      Lines = []
      Prefix = []
      Prefix.append('Watch-Only Root ID:');  Lines.append(self.wltRootIDConcat)
      Prefix.append('Watch-Only Root:');     Lines.append(self.pkccET16Lines[0])
      Prefix.append('');                     Lines.append(self.pkccET16Lines[1])
      Prefix.append('');                     Lines.append(self.pkccET16Lines[2])
      Prefix.append('');                     Lines.append(self.pkccET16Lines[3])

      # Draw the prefix data.
      origX, origY = self.scene.getCursorXY()
      self.scene.moveCursor(10, 0)
      colRect, rowHgt = self.scene.drawColumn(['<b>' + l + '</b>' \
                                               for l in Prefix])

      # Draw the data.
      nudgeDown = 2  # because the differing font size makes it look unaligned
      self.scene.moveCursor(10, nudgeDown)
      self.scene.drawColumn(Lines,
                              font=GETFONT('Fixed', 8, bold=True), \
                              rowHeight=rowHgt,
                              useHtml=False)

      # Draw the rectangle around the data.
      self.scene.moveCursor(MARGIN, colRect.y() - 2, absolute=True)
      width = self.scene.pageRect().width() - 2 * MARGIN
      self.scene.drawRect(width, colRect.height() + 7, \
                          edgeColor=QtGui.QColor(0, 0, 0), fillColor=None)

      # Draw the QR-related text below the data.
      self.scene.newLine(extra_dy=30)
      self.scene.drawText(self.tr(
         'The following QR code is for convenience only.  It contains the '
         'exact same data as the five lines above.  If you copy this data '
         'by hand, you can safely ignore this QR code.'), wrapWidth=4 * INCH)

      # Draw the QR code.
      self.scene.moveCursor(20, 0)
      x, y = self.scene.getCursorXY()
      edgeRgt = self.scene.pageRect().width() - MARGIN
      edgeBot = self.scene.pageRect().height() - MARGIN
      qrSize = max(1.5 * INCH, min(edgeRgt - x, edgeBot - y, 2.0 * INCH))
      self.scene.drawQR('\n'.join(Lines), qrSize)
      self.scene.newLine(extra_dy=25)

      # Clear the data and create a vertical scroll bar.
      Lines = None
      vbar = self.view.verticalScrollBar()
      vbar.setValue(vbar.minimum())
      self.view.update()


def checkSecurePrintCode(context, SECPRINT, securePrintCode):
   result = True
   try:
      if len(securePrintCode) < 9:
         QtWidgets.QMessageBox.critical(context, context.tr('Invalid Code'), context.trUtf8(
            u'You didn\'t enter a full SecurePrint\u200b\u2122 code.  This '
            'code is needed to decrypt your backup file.'), QtWidgets.QMessageBox.Ok)
         result = False
      elif not SECPRINT['FUNC_CHKPWD'](securePrintCode):
         QtWidgets.QMessageBox.critical(context, context.trUtf8(u'Bad SecurePrint\u200b\u2122 Code'), context.trUtf8(
            u'The SecurePrint\u200b\u2122 code you entered has an error '
            'in it.  Note that the code is case-sensitive.  Please verify '
            'you entered it correctly and try again.'), QtWidgets.QMessageBox.Ok)
         result = False
   except NonBase58CharacterError as e:
      QtWidgets.QMessageBox.critical(context, context.trUtf8(u'Bad SecurePrint\u200b\u2122 Code'), context.trUtf8(
         u'The SecurePrint\u200b\u2122 code you entered has unrecognized characters '
         'in it.  %s Only the following characters are allowed: %s' % (e.message, BASE58CHARS)), QtWidgets.QMessageBox.Ok)
      result = False
   return result



##########################################################################
class DlgShowTestResults(ArmoryDialog):
   #######################################################################
   def __init__(self, parent, isRandom, subsAndIDs, M, nFrag, expectID):
      super(DlgShowTestResults, self).__init__(parent, parent.main)

      accumSet = set()
      for sub, ID in subsAndIDs:
         accumSet.add(ID)

      allEqual = (len(accumSet) == 1)
      allCorrect = True
      testID = expectID
      if not testID:
         testID = subsAndIDs[0][1]

      allCorrect = testID == subsAndIDs[0][1]

      descr = ''
      nSubs = len(subsAndIDs)
      fact = lambda x: math.factorial(x)
      total = fact(nFrag) // (fact(M) * fact(nFrag - M))
      if isRandom:
         descr = self.tr(
            'The total number of fragment subsets (%d) is too high '
            'to test and display.  Instead, %d subsets were tested '
            'at random.  The results are below ' % (total, nSubs))
      else:
         descr = self.tr(
            'For the fragments you entered, there are a total of '
            '%d possible subsets that can restore your wallet. '
            'The test results for all subsets are shown below' % total)

      lblDescr = QRichLabel(descr)

      lblWltIDDescr = QRichLabel(self.tr(
         'The wallet ID is computed from the first '
         'address in your wallet based on the root key data (and the '
         '"chain code").  Therefore, a matching wallet ID proves that '
         'the wallet will produce identical addresses.'))


      frmResults = QtWidgets.QFrame()
      layout = QtWidgets.QGridLayout()
      row = 0
      for sub, ID in subsAndIDs:
         subStrs = [str(s) for s in sub]
         subText = ', '.join(subStrs[:-1])
         dispTxt = self.tr(
            'Fragments <b>%s</b> and <b>%s</b> produce a '
            'wallet with ID "<b>%s</b>"' % (subText, subStrs[-1], ID))

         chk = lambda: QtGui.QPixmap('./img/checkmark32.png').scaled(20, 20)
         _X_ = lambda: QtGui.QPixmap('./img/red_X.png').scaled(16, 16)

         lblTxt = QRichLabel(dispTxt)
         lblTxt.setWordWrap(False)
         lblPix = QtWidgets.QLabel('')
         lblPix.setPixmap(chk() if ID == testID else _X_())
         layout.addWidget(lblTxt, row, 0)
         layout.addWidget(lblPix, row, 1)
         row += 1



      scrollResults = QtWidgets.QScrollArea()
      frmResults = QtWidgets.QFrame()
      frmResults.setLayout(layout)
      scrollResults.setWidget(frmResults)

      btnOkay = QtWidgets.QPushButton(self.tr('Ok'))
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(btnOkay, QtWidgets.QDialogButtonBox.AcceptRole)
      self.connect(btnOkay, SIGNAL(CLICKED), self.accept)

      mainLayout = QtWidgets.QVBoxLayout()
      mainLayout.addWidget(lblDescr)
      mainLayout.addWidget(scrollResults)
      mainLayout.addWidget(lblWltIDDescr)
      mainLayout.addWidget(buttonBox)
      self.setLayout(mainLayout)

      self.setWindowTitle(self.tr('Fragment Test Results'))
      self.setMinimumWidth(500)

#################################################################################
class DlgFactoryReset(ArmoryDialog):
   def __init__(self, main=None, parent=None):
      super(DlgFactoryReset, self).__init__(parent, main)

      lblDescr = QRichLabel(self.tr(
         '<b><u>Armory Factory Reset</u></b> '
         '<br><br>'
         'It is <i>strongly</i> recommended that you make backups of your '
         'wallets before continuing, though <b>wallet files will never be '
         'intentionally deleted!</b>  All Armory '
         'wallet files, and the wallet.dat file used by Bitcoin Core/bitcoind '
         'should remain untouched in their current locations.  All Armory '
         'wallets will automatically be detected and loaded after the reset. '
         '<br><br>'
         'If you are not sure which option to pick, try the "lightest option" '
         'first, and see if your problems are resolved before trying the more '
         'extreme options.'))



      self.rdoSettings = QtWidgets.QRadioButton()
      self.lblSettingsText = QRichLabel(self.tr(
         '<b>Delete settings and rescan (lightest option)</b>'))
      self.lblSettings = QRichLabel(self.tr(
         'Only delete the settings file and transient network data.  The '
         'databases built by Armory will be rescanned (about 5-45 minutes)'))

      self.rdoArmoryDB = QtWidgets.QRadioButton()
      self.lblArmoryDBText = QRichLabel(self.tr('<b>Also delete databases and rebuild</b>'))
      self.lblArmoryDB = QRichLabel(self.tr(
         'Will delete settings, network data, and delete Armory\'s databases. The databases '
         'will be rebuilt and rescanned (45 min to 3 hours)'))

      self.rdoBitcoinDB = QtWidgets.QRadioButton()
      self.lblBitcoinDBText = QRichLabel(self.tr('<b>Also re-download the blockchain (extreme)</b>'))
      self.lblBitcoinDB = QRichLabel(self.tr(
         'This will delete settings, network data, Armory\'s databases, '
         '<b>and</b> Bitcoin Core\'s databases.  Bitcoin Core will '
         'have to download the blockchain again. This can take 8-72 hours depending on your '
         'system\'s speed and connection.  Only use this if you '
         'suspect blockchain corruption, such as receiving StdOut/StdErr errors '
         'on the dashboard.'))


      self.chkSaveSettings = QtWidgets.QCheckBox(self.tr('Do not delete settings files'))


      optFrames = []
      for rdo,txt,lbl in [ \
            [self.rdoSettings,  self.lblSettingsText,  self.lblSettings], \
            [self.rdoArmoryDB,  self.lblArmoryDBText,  self.lblArmoryDB], \
            [self.rdoBitcoinDB, self.lblBitcoinDBText, self.lblBitcoinDB]]:

         optLayout = QtWidgets.QGridLayout()
         txt.setWordWrap(False)
         optLayout.addWidget(makeHorizFrame([rdo, txt, 'Stretch']))
         optLayout.addWidget(lbl, 1,0, 1,3)
         if len(optFrames)==2:
            # Add option to disable deleting settings, on most extreme option
            optLayout.addWidget(self.chkSaveSettings, 2,0, 1,3)
         optFrames.append(QtWidgets.QFrame())
         optFrames[-1].setLayout(optLayout)
         optFrames[-1].setFrameStyle(STYLE_RAISED)


      self.rdoSettings.setChecked(True)

      btngrp = QtWidgets.QButtonGroup(self)
      btngrp.addButton(self.rdoSettings)
      btngrp.addButton(self.rdoArmoryDB)
      btngrp.addButton(self.rdoBitcoinDB)

      frmDescr = makeHorizFrame([lblDescr], STYLE_SUNKEN)
      frmOptions = makeVertFrame(optFrames, STYLE_SUNKEN)

      self.btnOkay = QtWidgets.QPushButton(self.tr('Continue'))
      self.btnCancel = QtWidgets.QPushButton(self.tr('Cancel'))
      buttonBox = QtWidgets.QDialogButtonBox()
      buttonBox.addButton(self.btnOkay, QtWidgets.QDialogButtonBox.AcceptRole)
      buttonBox.addButton(self.btnCancel, QtWidgets.QDialogButtonBox.RejectRole)
      self.connect(self.btnOkay, SIGNAL(CLICKED), self.clickedOkay)
      self.connect(self.btnCancel, SIGNAL(CLICKED), self.reject)

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(frmDescr)
      layout.addWidget(frmOptions)
      layout.addWidget(buttonBox)

      self.setLayout(layout)
      self.setMinimumWidth(600)
      self.setWindowTitle(self.tr('Factory Reset'))
      self.setWindowIcon(QtGui.QIcon(self.main.iconfile))



   ###
   def clickedOkay(self):


      if self.rdoSettings.isChecked():
         reply = QtWidgets.QMessageBox.warning(self, self.tr('Confirmation'), self.tr(
            'You are about to delete your settings and force Armory to rescan '
            'its databases.  Are you sure you want to do this?'), \
            QtWidgets.QMessageBox.Cancel | QtWidgets.QMessageBox.Ok)

         if not reply==QtWidgets.QMessageBox.Ok:
            self.reject()
            return

         touchFile( os.path.join(ARMORY_HOME_DIR, 'rescan.flag') )
         touchFile( os.path.join(ARMORY_HOME_DIR, 'clearmempool.flag'))
         touchFile( os.path.join(ARMORY_HOME_DIR, 'delsettings.flag'))
         self.accept()

      elif self.rdoArmoryDB.isChecked():
         reply = QtWidgets.QMessageBox.warning(self, self.tr('Confirmation'), self.tr(
            'You are about to delete your settings and force Armory to delete '
            'and rebuild its databases.  Are you sure you want to do this?'), \
            QtWidgets.QMessageBox.Cancel | QtWidgets.QMessageBox.Ok)

         if not reply==QtWidgets.QMessageBox.Ok:
            self.reject()
            return

         touchFile( os.path.join(ARMORY_HOME_DIR, 'rebuild.flag') )
         touchFile( os.path.join(ARMORY_HOME_DIR, 'clearmempool.flag'))
         touchFile( os.path.join(ARMORY_HOME_DIR, 'delsettings.flag'))
         self.accept()

      elif self.rdoBitcoinDB.isChecked():
         msg = 'delete your settings and '

         if self.chkSaveSettings.isChecked():
            msg = self.tr(
               'You are about to delete <b>all</b> '
               'blockchain databases on your system.  The Bitcoin software will '
               'have to redownload all of the blockchain data over the peer-to-peer '
               'network again. This can take from 8 to 72 hours depending on '
               'your system\'s speed and connection.  <br><br><b>Are you absolutely '
               'sure you want to do this?</b>')
         else:
            msg = self.tr(
               'You are about to delete your settings and delete <b>all</b> '
               'blockchain databases on your system.  The Bitcoin software will '
               'have to redownload all of the blockchain data over the peer-to-peer '
               'network again. This can take from 8 to 72 hours depending on '
               'your system\'s speed and connection.  <br><br><b>Are you absolutely '
               'sure you want to do this?</b>')

         reply = QtWidgets.QMessageBox.warning(self, self.tr('Confirmation'), msg, \
            QtWidgets.QMessageBox.Cancel | QtWidgets.QMessageBox.Yes)

         if not reply==QtWidgets.QMessageBox.Yes:
            QtWidgets.QMessageBox.warning(self, self.tr('Aborted'), self.tr(
                  'You canceled the factory reset operation.  No changes were '
                  'made.'), QtWidgets.QMessageBox.Ok)
            self.reject()
            return


         if not self.main.settings.get('ManageSatoshi'):
            # Must have user shutdown Bitcoin sw now, and delete DBs now
            reply = MsgBoxCustom(MSGBOX.Warning, self.tr('Restart Armory'), self.tr(
               '<b>Bitcoin Core (or bitcoind) must be closed to do the reset!</b> '
               'Please close all Bitcoin software, <u><b>right now</b></u>, '
               'before clicking "Continue". '
               '<br><br>'
               'Armory will now close.  Please restart Bitcoin Core/bitcoind '
               'first and wait for it to finish synchronizing before restarting '
               'Armory.'), wCancel=True, yesStr="Continue")

            if not reply:
               QtWidgets.QMessageBox.warning(self, self.tr('Aborted'), self.tr(
                  'You canceled the factory reset operation.  No changes were '
                  'made.'), QtWidgets.QMessageBox.Ok)
               self.reject()
               return

            # Do the delete operation now
            deleteBitcoindDBs()
         else:
            reply = QtWidgets.QMessageBox.warning(self, self.tr('Restart Armory'), self.tr(
               'Armory will now close to apply the requested changes.  Please '
               'restart it when you are ready to start the blockchain download '
               'again.'), QtWidgets.QMessageBox.Ok)

            if not reply == QtWidgets.QMessageBox.Ok:
               QtWidgets.QMessageBox.warning(self, self.tr('Aborted'), self.tr(
                  'You canceled the factory reset operation.  No changes were '
                  'made.'), QtWidgets.QMessageBox.Ok)
               self.reject()
               return

            touchFile( os.path.join(ARMORY_HOME_DIR, 'redownload.flag') )

         #  Always flag the rebuild, and del mempool and settings
         touchFile( os.path.join(ARMORY_HOME_DIR, 'rebuild.flag') )
         touchFile( os.path.join(ARMORY_HOME_DIR, 'clearmempool.flag'))
         if not self.chkSaveSettings.isChecked():
            touchFile( os.path.join(ARMORY_HOME_DIR, 'delsettings.flag'))
         self.accept()


      QtWidgets.QMessageBox.information(self, self.tr('Restart Armory'), self.tr(
         'Armory will now close so that the requested changes can '
         'be applied.'), QtWidgets.QMessageBox.Ok)
      self.accept()


#################################################################################
class DlgForkedImports(ArmoryDialog):
   def __init__(self, walletList, main=None, parent=None):
      super(DlgForkedImports, self).__init__(parent, main)

      descr1 = self.tr('<h2 style="color: red; text-align: center;">Forked imported addresses have been \
      detected in your wallets!!!</h2>')

      descr2 = self.tr('The following wallets have forked imported addresses: <br><br><b>') + \
      '<br>'.join(walletList) + '</b>'

      descr3 = self.tr('When you fix a corrupted wallet, any damaged private keys will be off \
      the deterministic chain. It means these private keys cannot be recreated \
      by your paper backup. If such private keys are encountered, Armory saves \
      them as forked imported private keys after it fixes the relevant wallets.')

      descr4 = self.tr('<h1 style="color: orange;"> - Do not accept payments to these wallets anymore<br>\
      - Do not delete or overwrite these wallets. <br> \
      - Transfer all funds to a fresh and backed up wallet</h1>')

      lblDescr1 = QRichLabel(descr1)
      lblDescr2 = QRichLabel(descr2)
      lblDescr3 = QRichLabel(descr3)
      lblDescr4 = QRichLabel(descr4)

      layout2 = QtWidgets.QVBoxLayout()
      layout2.addWidget(lblDescr2)
      frame2 = QtWidgets.QFrame()
      frame2.setLayout(layout2)
      frame2.setFrameStyle(QtWidgets.QFrame.StyledPanel)

      layout4 = QtWidgets.QVBoxLayout()
      layout4.addWidget(lblDescr4)
      frame4 = QtWidgets.QFrame()
      frame4.setLayout(layout4)
      frame4.setFrameStyle(QtWidgets.QFrame.StyledPanel)


      self.btnOk = QtWidgets.QPushButton('Ok')
      self.connect(self.btnOk, SIGNAL('clicked()'), self.accept)


      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblDescr1)
      layout.addWidget(frame2)
      layout.addWidget(lblDescr3)
      layout.addWidget(frame4)
      layout.addWidget(self.btnOk)


      self.setLayout(layout)
      self.setMinimumWidth(600)
      self.setWindowTitle(self.tr('Forked Imported Addresses'))
###


################################################################################
class DlgBroadcastBlindTx(ArmoryDialog):
   def __init__(self, main=None, parent=None):
      super(DlgBroadcastBlindTx, self).__init__(parent, main)

      self.pytx = None
      self.txList = []

      lblDescr = QRichLabel(self.tr(
         'Copy a raw, hex-encoded transaction below to have Armory '
         'broadcast it to the Bitcoin network.  This function is '
         'provided as a convenience to expert users, and carries '
         'no guarantees of usefulness. '
         '<br><br>'
         'Specifically, be aware of the following limitations of '
         'this broadcast function: '
         '<ul>'
         '<li>The transaction will be "broadcast" by sending it '
         'to the connected Bitcon Core instance which will '
         'forward it to the rest of the Bitcoin network. '
         'However, if the transaction is non-standard or '
         'does not satisfy standard fee rules, Bitcoin Core '
         '<u>will</u> drop it and it '
         'will never be seen by the Bitcoin network. '
         '</li>'
         '<li>There will be no feedback as to whether the '
         'transaction succeeded.  You will have to verify the '
         'success of this operation via other means. '
         'However, if the transaction sends '
         'funds directly to or from an address in one of your '
         'wallets, it will still generate a notification and show '
         'up in your transaction history for that wallet. '
         '</li>'
         '</ul>'))

      self.txtRawTx = QPlainTextEdit()
      self.txtRawTx.setFont(GETFONT('Fixed', 9))
      w,h = relaxedSizeNChar(self.txtRawTx, 90)
      self.txtRawTx.setMinimumWidth(w)
      self.txtRawTx.setMinimumHeight(h*5)
      self.connect(self.txtRawTx, SIGNAL('textChanged()'), self.txChanged)

      lblTxInfo = QRichLabel(self.tr('Parsed Transaction:'))

      self.txtTxInfo = QPlainTextEdit()
      self.txtTxInfo.setFont(GETFONT('Fixed', 9))
      self.txtTxInfo.setMinimumWidth(w)
      self.txtTxInfo.setMinimumHeight(h*7)
      self.txtTxInfo.setReadOnly(True)

      self.lblInvalid = QRichLabel('')

      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.btnAdd    = QtWidgets.QPushButton(self.tr("Add"))
      self.btnBroad  = QtWidgets.QPushButton(self.tr("Broadcast"))
      self.btnBroad.setEnabled(False)
      self.connect(self.btnCancel, SIGNAL('clicked()'), self.reject)
      self.connect(self.btnAdd, SIGNAL('clicked()'), self.addTx)
      self.connect(self.btnBroad, SIGNAL('clicked()'), self.doBroadcast)
      frmButtons = makeHorizFrame(['Stretch', self.btnCancel, self.btnAdd, self.btnBroad])

      layout = QtWidgets.QVBoxLayout()
      layout.addWidget(lblDescr)
      layout.addWidget(self.txtRawTx)
      layout.addWidget(lblTxInfo)
      layout.addWidget(self.txtTxInfo)
      layout.addWidget(self.lblInvalid)
      layout.addWidget(HLINE())
      layout.addWidget(frmButtons)

      self.setLayout(layout)
      self.setWindowTitle(self.tr("Broadcast Raw Transaction"))


   #############################################################################
   def txChanged(self):
      try:
         txt = str(self.txtRawTx.toPlainText()).strip()
         print (txt)
         txt = ''.join(txt.split())  # removes all whitespace
         self.pytx = PyTx().unserialize(hex_to_binary(txt))
         self.txtTxInfo.setPlainText(self.pytx.toString())
         LOGINFO('Valid tx entered:')
         LOGINFO(self.pytx.toString())
         self.setReady(True)
      except:
         LOGEXCEPT('Failed to parse tx')
         self.setReady(False)
         self.pytx = None

   #############################################################################
   def addTx(self):
      if self.pytx is not None:
         self.txList.append(self.pytx)

      self.txtRawTx.clear()

   #############################################################################
   def setReady(self, isTrue):
      self.btnBroad.setEnabled(isTrue)
      self.lblInvalid.setText("")
      if not isTrue:
         self.txtTxInfo.setPlainText('')
         if len(str(self.txtRawTx.toPlainText()).strip()) > 0:
            self.lblInvalid.setText(self.tr('<font color="%s"><b>Raw transaction '
            'is invalid!</font></b>' % htmlColor('TextWarn')))


   #############################################################################
   def doBroadcast(self):
      if self.pytx is not None:
         self.txList.append(self.pytx)

      rawTxList = []
      for txObj in self.txList:
         rawTxList.append(txObj.serialize())

      TheBridge.broadcastTx(rawTxList)

      ''' TODO: replace this with a tx broadcast successful popup notification
      txhash = self.pytx.getHash()


      hexhash = binary_to_hex(txhash, endOut=BIGENDIAN)
      if USE_TESTNET:
         linkToExplorer = 'https://blockexplorer.com/testnet/tx/%s' % hexhash
         dispToExplorer = 'https://blockexplorer.com/testnet/tx/%s...' % hexhash[:16]
      elif USE_REGTEST:
         linkToExplorer = ''
         dispToExplorer = ''
      else:
         linkToExplorer = 'https://blockchain.info/search/%s' % hexhash
         dispToExplorer = 'https://blockchain.info/search/%s...' % hexhash[:16]

      QtWidgets.QMessageBox.information(self, self.tr("Broadcast!"), self.tr(
         'Your transaction was successfully sent to the local Bitcoin '
         'Core instance, though there is no guarantees that it was '
         'forwarded to the rest of the network.   On testnet, just about '
         'every valid transaction will successfully propagate.  On the '
         'main Bitcoin network, this will fail unless it was a standard '
         'transaction type. '
         'The transaction '
         'had the following hash: '
         '<br><br> '
         '%1 '
         '<br><br>'
         'You can check whether it was seen by other nodes on the network '
         'with the link below: '
         '<br><br>'
         '<a href="%2">%3</a>').arg(hexhash, linkToExplorer, dispToExplorer), QtWidgets.QMessageBox.Ok)
      '''
      self.accept()

#################################################################################
class ArmorySplashScreen(QtWidgets.QSplashScreen):
   def __init__(self, pixLogo):
      super(ArmorySplashScreen, self).__init__(pixLogo)

      css = """
            QtWidgets.QProgressBar{ text-align: center; font-size: 8px; }
            """
      self.setStyleSheet(css)

      self.progressBar = QtWidgets.QProgressBar(self)
      self.progressBar.setMaximum(100)
      self.progressBar.setMinimum(0)
      self.progressBar.setValue(0)
      self.progressBar.setMinimumWidth(self.width())
      self.progressBar.setMaximumHeight(10)
      self.progressBar.setFormat(self.tr("Loading: %d%s" % (int(self.progressBar.value()) * 10, "%")))

   def updateProgress(self, val):
      self.progressBar.setValue(val)

#############################################################################
class DlgRegAndTest(ArmoryDialog):
   def __init__(self, parent=None, main=None):
      super(DlgRegAndTest, self).__init__(parent, main)

      self.btcClose = QtWidgets.QPushButton("Close")
      self.connect(self.btcClose, SIGNAL(CLICKED), self.close)
      btnBox = makeHorizFrame([STRETCH, self.btcClose])

      lblError = QRichLabel(self.tr('Error: You cannot run the Regression Test network and Bitcoin Test Network at the same time.'))

      dlgLayout = QtWidgets.QVBoxLayout()
      frmBtn = makeHorizFrame([STRETCH, self.btcClose])
      frmAll = makeVertFrame([lblError, frmBtn])

      dlgLayout.addWidget(frmAll)
      self.setLayout(dlgLayout)
      self.setWindowTitle('Error')

   def close(self):
      self.main.abortLoad = True
      LOGERROR('User attempted to run regtest and testnet simultaneously')
      super(DlgRegAndTest, self).reject()

#############################################################################
class URLHandler(QtCore.QObject):
   #@pyqtSignature("QUrl")
   def handleURL(self, link):
      DlgBrowserWarn(link.toString()).exec_()
