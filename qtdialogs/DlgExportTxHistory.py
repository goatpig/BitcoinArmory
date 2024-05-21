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

from qtpy import QtCore, QtWidgets
from armoryengine.BDM import TheBDM
from armoryengine.ArmoryUtils import str2coin, coin2str, unixTimeToFormatStr, \
   IGNOREZC, RightNow, hex_to_binary, hex_switchEndian, FORMAT_SYMBOLS, \
   DEFAULT_DATE_FORMAT
from qtdialogs.qtdefines import QRichLabel, HLINE, STRETCH, WLTTYPES, \
   makeHorizFrame, determineWalletType, createToolTipWidget
from qtdialogs.ArmoryDialog import ArmoryDialog

from armorymodels import LEDGERCOLS
from armoryengine.Transaction import getFeeForTx
from armoryengine.CppBridge import TheBridge


################################################################################
class DlgExportTxHistory(ArmoryDialog):
   def __init__(self, parent=None, main=None):
      super(DlgExportTxHistory, self).__init__(parent, main)

      self.reversedLBdict = {v:k for k,v in self.main.lockboxIDMap.items()}

      self.cmbWltSelect = QtWidgets.QComboBox()
      self.cmbWltSelect.clear()
      self.cmbWltSelect.addItem(self.tr('My Wallets'))
      self.cmbWltSelect.addItem(self.tr('Offline Wallets'))
      self.cmbWltSelect.addItem(self.tr('Other Wallets'))

      self.cmbWltSelect.insertSeparator(4)
      self.cmbWltSelect.addItem(self.tr('All Wallets'))
      self.cmbWltSelect.addItem(self.tr('All Lockboxes'))
      self.cmbWltSelect.addItem(self.tr('All Wallets & Lockboxes'))

      self.cmbWltSelect.insertSeparator(8)
      for wltID in self.main.walletIDList:
         self.cmbWltSelect.addItem(self.main.walletMap[wltID].getDisplayStr())

      self.cmbWltSelect.insertSeparator(8 + len(self.main.walletIDList))
      for idx in self.reversedLBdict:
         self.cmbWltSelect.addItem(self.main.allLockboxes[idx].shortName)


      self.cmbSortSelect = QtWidgets.QComboBox()
      self.cmbSortSelect.clear()
      self.cmbSortSelect.addItem(self.tr('Date (newest first)'))
      self.cmbSortSelect.addItem(self.tr('Date (oldest first)'))


      self.cmbFileFormat = QtWidgets.QComboBox()
      self.cmbFileFormat.clear()
      self.cmbFileFormat.addItem(self.tr('Comma-Separated Values (*.csv)'))


      fmt = self.main.getPreferredDateFormat()
      ttipStr = self.tr('Use any of the following symbols:<br>')
      fmtSymbols = [x[0] + ' = ' + x[1] for x in FORMAT_SYMBOLS]
      ttipStr += '<br>'.join(fmtSymbols)

      self.edtDateFormat = QtWidgets.QLineEdit()
      self.edtDateFormat.setText(fmt)
      self.ttipFormatDescr = createToolTipWidget(ttipStr)

      self.lblDateExample = QRichLabel('', doWrap=False)
      self.edtDateForma.textEdited(self.doExampleDate)
      self.doExampleDate()
      self.btnResetFormat = QtWidgets.QPushButton(self.tr("Reset to Default"))

      def doReset():
         self.edtDateFormat.setText(DEFAULT_DATE_FORMAT)
         self.doExampleDate()
      self.btnResetFormat.clicked.connect(doReset)


      # Add the usual buttons
      self.btnCancel = QtWidgets.QPushButton(self.tr("Cancel"))
      self.btnAccept = QtWidgets.QPushButton(self.tr("Export"))
      self.btnCancel.clicked.connect(self.reject)
      self.btnAccept.clicked.connect(self.accept)
      btnBox = makeHorizFrame([STRETCH, self.btnCancel, self.btnAccept])


      dlgLayout = QtWidgets.QGridLayout()

      i = 0
      dlgLayout.addWidget(QRichLabel(self.tr('Export Format:')), i, 0)
      dlgLayout.addWidget(self.cmbFileFormat, i, 1)

      i += 1
      dlgLayout.addWidget(HLINE(), i, 0, 1, 2)

      i += 1
      dlgLayout.addWidget(QRichLabel(self.tr('Wallet(s) to export:')), i, 0)
      dlgLayout.addWidget(self.cmbWltSelect, i, 1)

      i += 1
      dlgLayout.addWidget(HLINE(), i, 0, 1, 2)

      i += 1
      dlgLayout.addWidget(QRichLabel(self.tr('Sort Table:')), i, 0)
      dlgLayout.addWidget(self.cmbSortSelect, i, 1)
      i += 1
      dlgLayout.addWidget(HLINE(), i, 0, 1, 2)

      i += 1
      dlgLayout.addWidget(QRichLabel(self.tr('Date Format:')), i, 0)
      fmtfrm = makeHorizFrame([self.lblDateExample, STRETCH, self.ttipFormatDescr])
      dlgLayout.addWidget(fmtfrm, i, 1)

      i += 1
      dlgLayout.addWidget(self.btnResetFormat, i, 0)
      dlgLayout.addWidget(self.edtDateFormat, i, 1)

      i += 1
      dlgLayout.addWidget(HLINE(), i, 0, 1, 2)

      i += 1
      dlgLayout.addWidget(HLINE(), i, 0, 1, 2)

      i += 1
      dlgLayout.addWidget(btnBox, i, 0, 1, 2)

      self.setLayout(dlgLayout)


   #############################################################################
   def doExampleDate(self, qstr=None):
      fmtstr = str(self.edtDateFormat.text())
      try:
         exampleDateStr = unixTimeToFormatStr(1030501970, fmtstr)
         self.lblDateExample.setText(self.tr('Example: %s' % exampleDateStr))
         self.isValidFormat = True
      except:
         self.lblDateExample.setText(self.tr('Example: [[invalid date format]]'))
         self.isValidFormat = False

   #############################################################################
   def accept(self, *args):
      if self.createFile_CSV():
         super(DlgExportTxHistory, self).accept(*args)


   #############################################################################
   def createFile_CSV(self):
      if not self.isValidFormat:
         QtWidgets.QMessageBox.warning(self, self.tr('Invalid date format'), \
            self.tr('Cannot create CSV without a valid format for transaction '
            'dates and times'), QtWidgets.QMessageBox.Ok)
         return False

      COL = LEDGERCOLS

      # This was pretty much copied from the createCombinedLedger method...
      # I rarely do this, but modularizing this piece is a non-trivial
      wltIDList = []
      typelist = [[wid, determineWalletType(self.main.walletMap[wid], self.main)[0]] \
         for wid in self.main.walletIDList]
      currIdx = self.cmbWltSelect.currentIndex()
      if currIdx >= 8:
         idx = currIdx - 8
         if idx < len(self.main.walletIDList):
            #picked a single wallet
            wltIDList = [self.main.walletIDList[idx]]
         else:
            #picked a single lockbox
            idx -= len(self.main.walletIDList) +1
            wltIDList = [self.reversedLBdict[idx]]
      else:
         listOffline = [t[0] for t in filter(lambda x: x[1] == WLTTYPES.Offline, typelist)]
         listWatching = [t[0] for t in filter(lambda x: x[1] == WLTTYPES.WatchOnly, typelist)]
         listCrypt = [t[0] for t in filter(lambda x: x[1] == WLTTYPES.Crypt, typelist)]
         listPlain = [t[0] for t in filter(lambda x: x[1] == WLTTYPES.Plain, typelist)]
         lockboxIDList = [t for t in self.main.lockboxIDMap]

         if currIdx == 0:
            wltIDList = listOffline + listCrypt + listPlain
         elif currIdx == 1:
            wltIDList = listOffline
         elif currIdx == 2:
            wltIDList = listWatching
         elif currIdx == 4:
            wltIDList = self.main.walletIDList
         elif currIdx == 5:
            wltIDList = lockboxIDList
         elif currIdx == 6:
            wltIDList = self.main.walletIDList + lockboxIDList
         else:
            pass

      order = "ascending"
      sortTxt = str(self.cmbSortSelect.currentText())
      if 'newest' in sortTxt:
         order = "descending"

      totalFunds, spendFunds, unconfFunds = 0, 0, 0
      wltBalances = {}
      for wltID in wltIDList:
         if wltID in self.main.walletMap:
            wlt = self.main.walletMap[wltID]

            totalFunds += wlt.getBalance('Total')
            spendFunds += wlt.getBalance('Spendable')
            unconfFunds += wlt.getBalance('Unconfirmed')
            if order == "ascending":
               wltBalances[wltID] = 0   # will be accumulated
            else:
               wltBalances[wltID] = wlt.getBalance('Total')

         else:
            #lockbox
            cppwlt = self.main.cppLockboxWltMap[wltID]
            totalFunds += cppwlt.getFullBalance()
            spendFunds += cppwlt.getSpendableBalance(TheBDM.getTopBlockHeight(), IGNOREZC)
            unconfFunds += cppwlt.getUnconfirmedBalance(TheBDM.getTopBlockHeight(), IGNOREZC)
            if order == "ascending":
               wltBalances[wltID] = 0   # will be accumulated
            else:
               wltBalances[wltID] = cppwlt.getFullBalance()

      if order == "ascending":
         allBalances = 0
      else:
         allBalances = totalFunds


      #prepare csv file
      wltSelectStr = str(self.cmbWltSelect.currentText()).replace(' ', '_')
      timestampStr = unixTimeToFormatStr(RightNow(), '%Y%m%d_%H%M')
      filenamePrefix = 'ArmoryTxHistory_%s_%s' % (wltSelectStr, timestampStr)
      fmtstr = str(self.cmbFileFormat.currentText())
      if 'csv' in fmtstr:
         defaultName = filenamePrefix + '.csv'
         fullpath = self.main.getFileSave('Save CSV File', \
                                              ['Comma-Separated Values (*.csv)'], \
                                              defaultName)

         if len(fullpath) == 0:
            return

         f = open(fullpath, 'w')

         f.write(self.tr('Export Date: %s\n' % unixTimeToFormatStr(RightNow())))
         f.write(self.tr('Total Funds: %s\n' % coin2str(totalFunds, maxZeros=0).strip()))
         f.write(self.tr('Spendable Funds: %s\n' % coin2str(spendFunds, maxZeros=0).strip()))
         f.write(self.tr('Unconfirmed Funds: %s\n' % coin2str(unconfFunds, maxZeros=0).strip()))
         f.write('\n')

         f.write(self.tr('Included Wallets:\n'))
         for wltID in wltIDList:
            if wltID in self.main.walletMap:
               wlt = self.main.walletMap[wltID]
               f.write('%s,%s\n' % (wltID, wlt.labelName.replace(',', ';')))
            else:
               wlt = self.main.allLockboxes[self.main.lockboxIDMap[wltID]]
               f.write(self.tr('%s (lockbox),%s\n' % (wltID, wlt.shortName.replace(',', ';'))))
         f.write('\n')


         headerRow = [self.tr('Date'), self.tr('Transaction ID'),
                      self.tr('Number of Confirmations'), self.tr('Wallet ID'),
                      self.tr('Wallet Name'), self.tr('Credit'), self.tr('Debit'),
                      self.tr('Fee (paid by this wallet)'), self.tr('Wallet Balance'),
                      self.tr('Total Balance'), self.tr('Label')]

         f.write(','.join(str(header) for header in headerRow) + '\n')

         #get history
         historyLedger = TheBridge.getHistoryForWalletSelection(wltIDList, order)

         # Each value in COL.Amount will be exactly how much the wallet balance
         # increased or decreased as a result of this transaction.
         ledgerTable = self.main.convertLedgerToTable(historyLedger,
                                                         showSentToSelfAmt=True)

         # Sort the data chronologically first, compute the running balance for
         # each row, then sort it the way that was requested by the user.
         for row in ledgerTable:
            if row[COL.toSelf] == False:
               rawAmt = str2coin(row[COL.Amount])
            else:
               #if SentToSelf, balance and total rolling balance should only
               #take fee in account
               rawAmt, fee_byte = getFeeForTx(hex_to_binary(row[COL.TxHash]))
               rawAmt = -1 * rawAmt

            if order == "ascending":
               wltBalances[row[COL.WltID]] += rawAmt
               allBalances += rawAmt

            row.append(wltBalances[row[COL.WltID]])
            row.append(allBalances)

            if order == "descending":
               wltBalances[row[COL.WltID]] -= rawAmt
               allBalances -= rawAmt


         for row in ledgerTable:
            vals = []

            fmtstr = str(self.edtDateFormat.text())
            unixTime = row[COL.UnixTime]
            vals.append(unixTimeToFormatStr(unixTime, fmtstr))
            vals.append(hex_switchEndian(row[COL.TxHash]))
            vals.append(row[COL.NumConf])
            vals.append(row[COL.WltID])
            if row[COL.WltID] in self.main.walletMap:
               vals.append(self.main.walletMap[row[COL.WltID]].labelName.replace(',', ';'))
            else:
               lbId = self.main.lockboxIDMap[row[COL.WltID]]
               vals.append(self.main.allLockboxes[lbId].shortName.replace(',', ';'))

            wltEffect = row[COL.Amount]
            txFee, fee_byte = getFeeForTx(hex_to_binary(row[COL.TxHash]))
            if float(wltEffect) >= 0:
               if row[COL.toSelf] == False:
                  vals.append(wltEffect.strip())
                  vals.append('')
                  vals.append('')
               else:
                  vals.append(wltEffect.strip() + ' (STS)')
                  vals.append('')
                  vals.append(coin2str(txFee).strip())
            else:
               vals.append('')
               vals.append(wltEffect.strip()[1:]) # remove negative sign
               vals.append(coin2str(txFee).strip())

            vals.append(coin2str(row[-2]))
            vals.append(coin2str(row[-1]))
            vals.append(row[COL.Comment])

            f.write('%s,%s,%d,%s,%s,%s,%s,%s,%s,%s,"%s"\n' % tuple(vals))

      f.close()
      return True
