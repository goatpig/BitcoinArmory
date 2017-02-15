<?xml version="1.0" ?><!DOCTYPE TS><TS language="de" version="2.0">
<context>
    <name>@default</name>
    <message>
        <location filename="ArmoryQt.py" line="582"/>
        <source>Not Online</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1324"/>
        <source>Copy or load a transaction from file into the text box below.  If the transaction is unsigned and you have the correct wallet, you will have the opportunity to sign it.  If it is already signed you will have the opportunity to broadcast it to the Bitcoin network to make it final.</source>
        <translation>Kopiere oder lade eine Transaktion von einer Datei in die Textbox unterhalb. Falls die Transaktion nicht signiert ist und du das richtige Wallet hast, wirst du die Möglichkeit haben die Transaktion zu signieren. Falls die Transaktion bereits signiert ist, wirst du sie ins Bitcoin Netzwerk übermitteln können und sie damit vollenden.</translation>
    </message>
    <message>
        <location filename="TxFrames.py" line="1387"/>
        <source>A unique string that identifies an &lt;i&gt;unsigned&lt;/i&gt; transaction.  This is different than the ID that the transaction will have when it is finally broadcast, because the broadcast ID cannot be calculated without all the signatures</source>
        <translation>Ein eindeutiger String identifiziert eine &lt;i&gt;unsignierte&lt;i&gt; Transaktion. Der ist anders als die ID, die die Transaktion nach der endgültige Übermittlung erhalten wird, weil die   Übermittlungs-ID erst nach dem Vorliegen aller Unterschriften berechnet werden kann.</translation>
    </message>
    <message>
        <location filename="WalletFrames.py" line="894"/>
        <source>Protects Imported Addresses</source>
        <translation>Schützt importierte Adressen</translation>
    </message>
</context>
<context>
    <name>AddressTreeModel</name>
    <message>
        <location filename="TreeViewGUI.py" line="602"/>
        <source>Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TreeViewGUI.py" line="603"/>
        <source>Comment</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TreeViewGUI.py" line="604"/>
        <source>Tx Count</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TreeViewGUI.py" line="605"/>
        <source>Balance</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>AddressTypeSelectDialog</name>
    <message>
        <location filename="AddressTypeSelectDialog.py" line="24"/>
        <source>P2PKH Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="AddressTypeSelectDialog.py" line="25"/>
        <source>Legacy Armory address type. Backwards compatible.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="AddressTypeSelectDialog.py" line="40"/>
        <source>P2SH-P2WPKH address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="AddressTypeSelectDialog.py" line="41"/>
        <source>P2WPKH (SegWit script) nested in P2SH script. Any wallet can pay to this address. Only wallets supporting SegWit can spend from it.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="AddressTypeSelectDialog.py" line="61"/>
        <source>P2SH-P2PK address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="AddressTypeSelectDialog.py" line="62"/>
        <source>Compressed P2PK script nested in P2SH output. Any wallet can pay to this address. Only Armory 0.96+ can spend from it.&lt;br&gt;&lt;br&gt;This format allow for more efficient transaction space use, resulting in smaller inputs and lower fees.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="AddressTypeSelectDialog.py" line="86"/>
        <source>Apply</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="AddressTypeSelectDialog.py" line="87"/>
        <source>Cancel</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="AddressTypeSelectDialog.py" line="96"/>
        <source>Select Address Type</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>AdvancedOptionsFrame</name>
    <message>
        <location filename="WalletFrames.py" line="413"/>
        <source>Armory will test your system's speed to determine the most challenging encryption settings that can be applied in a given amount of time.  High settings make it much harder for someone to guess your passphrase.  This is used for all encrypted wallets, but the default parameters can be changed below.
</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="420"/>
        <source>This is the amount of time it will take for your computer to unlock your wallet after you enter your passphrase. (the actual time used will be less than the specified time, but more than one half of it).  </source>
        <translation>Das ist die Zeit, die dein Rechner braucht um dein Wallet zu entsperren, nachdem du dein Passwort eingegeben hast (die tatsächlich verwendete Zeit wird kürzer als die festgelegte Zeit, aber mehr als die Hälfte sein).</translation>
    </message>
    <message>
        <location filename="WalletFrames.py" line="430"/>
        <source>Target compute &amp;time (s, ms):</source>
        <translation>Ziel berechnen &amp;Zeit (s, ms):</translation>
    </message>
    <message>
        <location filename="WalletFrames.py" line="431"/>
        <source>This is the &lt;b&gt;maximum&lt;/b&gt; memory that will be used as part of the encryption process.  The actual value used may be lower, depending on your system&apos;s speed.  If a low value is chosen, Armory will compensate by chaining together more calculations to meet the target time.  High memory target will make GPU-acceleration useless for guessing your passphrase.</source>
        <translation>Das ist die &lt;b&gt;maximale&lt;b&gt; Speichergröße, die als Teil des Verschlüsselungsprozesses verwendet wird. Der tatsächliche Wert könnte, abhängig von der Geschwindigkeit deines Rechners, kleiner sein. Falls ein niedriger Wert gewählt ist, wird Armory durch die Verbindung mehreren Berechnungen kompensieren, um die Zielzeit zu erreichen. Ein höherer Speicherwert wird die GPU-Beschleunigung für das Erraten deines Passwortes nutzlos machen.</translation>
    </message>
    <message>
        <location filename="WalletFrames.py" line="445"/>
        <source>Max &amp;memory usage (kB, MB):</source>
        <translation>Maximale &amp;Speicherverwendung (kB, MB):</translation>
    </message>
</context>
<context>
    <name>AllWalletsDispModel</name>
    <message>
        <location filename="armorymodels.py" line="114"/>
        <source>ID</source>
        <translation>ID</translation>
    </message>
    <message>
        <location filename="armorymodels.py" line="114"/>
        <source>Wallet Name</source>
        <translation>Wallet Name</translation>
    </message>
    <message>
        <location filename="armorymodels.py" line="114"/>
        <source>Security</source>
        <translation>Sicherheit</translation>
    </message>
    <message>
        <location filename="armorymodels.py" line="114"/>
        <source>Balance</source>
        <translation>Guthaben</translation>
    </message>
</context>
<context>
    <name>ArmoryDialog</name>
    <message>
        <location filename="qtdefines.py" line="787"/>
        <source>Armory - Bitcoin Wallet Management [TESTNET] </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdefines.py" line="790"/>
        <source>Armory - Bitcoin Wallet Management [REGTEST] </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdefines.py" line="793"/>
        <source>Armory - Bitcoin Wallet Management</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>ArmoryMainWindow</name>
    <message>
        <location filename="ArmoryQt.py" line="298"/>
        <source>&lt;font color=%1&gt;Offline&lt;/font&gt; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="349"/>
        <source>Create Wallet</source>
        <translation>Wallet erstellen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="350"/>
        <source>Import or Restore Wallet</source>
        <translation>Wallet einlesen oder wiederherstellen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="355"/>
        <source>&lt;b&gt;Available Wallets:&lt;/b&gt;</source>
        <translation>&lt;b&gt;Verfügbare Wallets&lt;/b&gt;</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="385"/>
        <source>&lt;b&gt;Maximum Funds:&lt;/b&gt;</source>
        <translation>&lt;b&gt;Maximale Gelder:&lt;/b&gt;</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="386"/>
        <source>&lt;b&gt;Spendable Funds:&lt;/b&gt;</source>
        <translation>&lt;b&gt;Verfügbare Gelder:&lt;/b&gt;</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="387"/>
        <source>&lt;b&gt;Unconfirmed:&lt;/b&gt;</source>
        <translation>&lt;b&gt;Unbestätigt:&lt;/b&gt;</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="403"/>
        <source>
            Funds if all current transactions are confirmed.  
            Value appears gray when it is the same as your spendable funds. </source>
        <translation>
Betrag wenn alle Transaktionen bestätigt wurden.
Wert erscheint grau falls er mit dem verfügbaren übereinstimmt.</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="406"/>
        <source>Funds that can be spent &lt;i&gt;right now&lt;/i&gt;</source>
        <translation>Betrag der &lt;i&gt;momentan&lt;/i&gt; ausgegeben werden kann</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="407"/>
        <source>
            Funds that have less than 6 confirmations, and thus should not 
            be considered &lt;i&gt;yours&lt;/i&gt;, yet.</source>
        <translation>
Gelder die weniger als 6 Bestätigungen haben, und deshalb noch nicht
als &lt;i&gt;Eigentum&lt;/i&gt; angesehen werden sollten.</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="435"/>
        <source>Dashboard</source>
        <translation>Übersichtsseite</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1335"/>
        <source>Send Bitcoins</source>
        <translation>Bitcoins versenden</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1336"/>
        <source>Receive Bitcoins</source>
        <translation>Bitcoins erhalten</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="451"/>
        <source>Wallet Properties</source>
        <translation>Wallet Eigenschaften</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="452"/>
        <source>Offline Transactions</source>
        <translation>Offline Transaktionen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="453"/>
        <source>Lockboxes (Multi-Sig)</source>
        <translation>Lockboxen (Multi-Sig)</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="510"/>
        <source>&amp;File</source>
        <translation>&amp;Datei</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="511"/>
        <source>&amp;User</source>
        <translation>&amp;Benutzer</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="512"/>
        <source>&amp;Tools</source>
        <translation>&amp;Werkzeuge</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="513"/>
        <source>&amp;Addresses</source>
        <translation>&amp;Adressen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="514"/>
        <source>&amp;Wallets</source>
        <translation>&amp;Wallets</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="515"/>
        <source>&amp;MultiSig</source>
        <translation>&amp;MultiSig</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="516"/>
        <source>&amp;Help</source>
        <translation>&amp;Hilfe</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="522"/>
        <source>Transactions Unavailable</source>
        <translation>Transaktionen nicht verfügbar</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="522"/>
        <source>Transaction history cannot be collected until Armory is
               in online mode.  Please try again when Armory is online. </source>
        <translation>Transaktionsverlauf kann nicht gesammelt werden bis Armory
online ist. Bitte erneut versuchen wenn Armory online ist.</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="582"/>
        <source>
               Bitcoin Core is not available, so Armory will not be able
               to broadcast any transactions for you.</source>
        <translation>
Bitcoin Core ist nicht vorhanden, Armory ist nicht in der Lage
für dich Transaktionen zu übertragen.</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="590"/>
        <source>&amp;Message Signing/Verification...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="592"/>
        <source>&amp;EC Calculator...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="593"/>
        <source>&amp;Broadcast Raw Transaction...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5006"/>
        <source>Offline</source>
        <translation>Offline</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="602"/>
        <source>
               Armory is currently offline, and cannot determine what funds are
               available for simulfunding.  Please try again when Armory is in
               online mode.</source>
        <translation>

Armory ist momentan offline und kann nicht bestimmen welche Mittel
für simulfunding zur Verfügung stehen. Bitte versuchen Sie es  erneut, 
wenn Armory im online Modus ist.</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="611"/>
        <source>Import Multi-Spend Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="612"/>
        <source>
            Import a signature-collector text block for review and signing.
            It is usually a block of text with &quot;TXSIGCOLLECT&quot; in the first line,
            or a &lt;i&gt;*.sigcollect.tx&lt;/i&gt; file.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="625"/>
        <source>Simulfund &amp;Promissory Note</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="626"/>
        <source>Simulfund &amp;Collect &amp;&amp; Merge</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="627"/>
        <source>Simulfund &amp;Review &amp;&amp; Sign</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="634"/>
        <source>View &amp;Address Book...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="635"/>
        <source>&amp;Sweep Private Key/Address...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="636"/>
        <source>&amp;Import Private Key/Address...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="643"/>
        <source>&amp;Create New Wallet</source>
        <translation>&amp;Neue Wallet erstellen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="644"/>
        <source>&amp;Import or Restore Wallet</source>
        <translation>Wallet einlesen oder wiederherstellen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="645"/>
        <source>View &amp;Address Book</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="646"/>
        <source>&amp;Fix Damaged Wallet</source>
        <translation>&amp;Beschädigte Wallet reparieren</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="663"/>
        <source>&amp;About Armory...</source>
        <translation>&amp;Über Armory...</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="666"/>
        <source>Verify Signed Package...</source>
        <translation>Signiertes Paket Verifizieren...</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="669"/>
        <source>Clear All Unconfirmed</source>
        <translation>Entferne alle unbestätigten</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="670"/>
        <source>Rescan Databases</source>
        <translation>Datenbanken neu einlesen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="671"/>
        <source>Rebuild and Rescan Databases</source>
        <translation>Datenbanken erneuern und neu einlesen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="672"/>
        <source>Rescan Balance</source>
        <translation>Guthaben neu einlesen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="673"/>
        <source>Factory Reset</source>
        <translation>Auf Werkszustand zurücksetzen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="695"/>
        <source>Multi-Sig Lockboxes</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="696"/>
        <source>Lockbox &amp;Manager...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="755"/>
        <source>Default Data Directory</source>
        <translation>Standard Daten Verzeichnis</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="755"/>
        <source>
            Armory is using the default data directory because
            the data directory specified in the command line could
            not be found nor created.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="762"/>
        <source>Default Database Directory</source>
        <translation>Standard Datenbanken Verzeichnis</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="762"/>
        <source>
            Armory is using the default database directory because
            the database directory specified in the command line could
            not be found nor created.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="769"/>
        <source>Bitcoin Directory</source>
        <translation>Bitcoin Verzeichnis</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="769"/>
        <source>
            Armory is using the default Bitcoin directory because
            the Bitcoin director specified in the command line could
            not be found.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="776"/>
        <source>Delete Old DB Directory</source>
        <translation>Lösche alten DB Ordner</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="776"/>
        <source>Armory detected an older version Database.
                  Do you want to delete the old database? Choose yes if 
                  do not think that you will revert to an older version of Armory.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1525"/>
        <source>Do not ask this question again</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="974"/>
        <source>Bad Module</source>
        <translation>Ungültiges Modul</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="879"/>
        <source>
               The module you attempted to load (%1) is malformed.  It is
               missing attributes that are needed for Armory to load it.
               It will be skipped.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="983"/>
        <source>Outdated Module</source>
        <translation>Veraltetes Modul</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="888"/>
        <source>
               Module &quot;%1&quot; is only specified to work up to Armory version %2.
               You are using Armory version %3.  Please remove the module if
               you experience any problems with it, or contact the maintainer
               for a new version.
               &lt;br&gt;&lt;br&gt;
               Do you want to continue loading the module?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="945"/>
        <source>Invalid Module</source>
        <translation>Ungültiges Modul</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="945"/>
        <source>
                  Armory detected the following module which is
                  &lt;font color=%1&gt;&lt;b&gt;invalid&lt;/b&gt;&lt;/font&gt;:
                  &lt;br&gt;&lt;br&gt;
                     &lt;b&gt;Module Name:&lt;/b&gt; %2&lt;br&gt;
                     &lt;b&gt;Module Path:&lt;/b&gt; %3&lt;br&gt;
                  &lt;br&gt;&lt;br&gt;
                  Armory will only run a module from a zip file that
                  has the required stucture.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="955"/>
        <source>UNSIGNED Module</source>
        <translation>NICHT UNTERZEICHNETES Modul</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="955"/>
        <source>
                  Armory detected the following module which
                  &lt;font color=&quot;%1&quot;&gt;&lt;b&gt;has not been signed by Armory&lt;/b&gt;&lt;/font&gt; and may be dangerous:
                  &lt;br&gt;&lt;br&gt;
                     &lt;b&gt;Module Name:&lt;/b&gt; %2&lt;br&gt;
                     &lt;b&gt;Module Path:&lt;/b&gt; %3&lt;br&gt;
                  &lt;br&gt;&lt;br&gt;
                  Armory will not allow you to run this module.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="974"/>
        <source>
                     The module you attempted to load (%1) is malformed.  It is
                     missing attributes that are needed for Armory to load it.
                     It will be skipped.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="983"/>
        <source>
                     Module %1 is only specified to work up to Armory version %2.
                     You are using Armory version %3.  Please remove the module if
                     you experience any problems with it, or contact the maintainer
                     for a new version.
                     &lt;br&gt;&lt;br&gt;
                     Do you want to continue loading the module?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1048"/>
        <source>
         The next time you restart Armory, all unconfirmed transactions will
         be cleared allowing you to retry any stuck transactions.</source>
        <translation>
Beim nächsten Start von Armory sind alle unbestätigten Transaktionen
entfernt, um hängen gebliebene Transaktionen neu senden zu können.</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1052"/>
        <source>
         &lt;br&gt;&lt;br&gt;Make sure you also restart Bitcoin Core
         (or bitcoind) and let it synchronize again before you restart
         Armory.  Doing so will clear its memory pool, as well</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1056"/>
        <source>Memory Pool</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1186"/>
        <source>Queue Rescan?</source>
        <translation>Warteschlange neu scannen?</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1186"/>
        <source>
         The next time you restart Armory, it will rescan the blockchain
         database, and reconstruct your wallet histories from scratch.
         The rescan will take 10-60 minutes depending on your system.
         &lt;br&gt;&lt;br&gt;
         Do you wish to force a rescan on the next Armory restart?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1198"/>
        <source>Queue Rebuild?</source>
        <translation>Warteschlange neu aufbauen?</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1198"/>
        <source>
         The next time you restart Armory, it will rebuild and rescan
         the entire blockchain database.  This operation can take between
         30 minutes and 4 hours depending on you system speed.
         &lt;br&gt;&lt;br&gt;
         Do you wish to force a rebuild on the next Armory restart?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1210"/>
        <source>Queue Balance Rescan?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1210"/>
        <source>
         The next time you restart Armory, it will rescan the balance of
         your wallets. This operation typically takes less than a minute
         &lt;br&gt;&lt;br&gt;
         Do you wish to force a balance rescan on the next Armory restart?</source>
        <translation>
Beim nächsten Start von Armory wird das Guthaben aller Wallets
überprüft. Dies sollte kaum eine Minute dauern
&lt;br&gt;&lt;br&gt;
Soll beim nächsten Start von Armory eine Guthaben Überprüfung erfolgen?</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1250"/>
        <source>Select Wallet</source>
        <translation>Wallet auswählen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1236"/>
        <source>You must import an address into a specific wallet.  If you do not want to import the key into any available wallet, it is recommeneded you make a new wallet for this purpose.&lt;br&gt;&lt;br&gt;Double-click on the desired wallet from the main window, then click on &quot;Import/Sweep Private Keys&quot; on the bottom-right of the properties window.&lt;br&gt;&lt;br&gt;Keys cannot be imported into watching-only wallets, only full wallets.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1250"/>
        <source>You must select a wallet into which funds will be swept. Double-click on the desired wallet from the main window, then click on &quot;Import/Sweep Private Keys&quot; on the bottom-right of the properties window to sweep to that wallet.&lt;br&gt;&lt;br&gt;Keys cannot be swept into watching-only wallets, only full wallets.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1334"/>
        <source>Show Armory</source>
        <translation>Armory anzeigen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1337"/>
        <source>Quit Armory</source>
        <translation>Armory beenden</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1525"/>
        <source>Default URL Handler</source>
        <translation>Standard URL Anwendung</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1454"/>
        <source>Armory is not set as your default application for handling
                  &quot;bitcoin:&quot; links.  Would you like to use Armory as the 
                  default?</source>
        <translation>Armory ist nicht als Standardanwendung für &quot;bitcoin:&quot; links 
ausgewählt. Soll Armory als Standard eingesetzt werden?</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1525"/>
        <source>Armory is not set as your default application for handling
               &quot;bitcoin:&quot; links.  Would you like to use Armory as the 
               default?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1578"/>
        <source>Version Warning</source>
        <translation>Version Warnung</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1578"/>
        <source>
            Since Armory version 0.92 the formats for offline transaction
            operations has changed to accommodate multi-signature
            transactions.  This format is &lt;u&gt;not&lt;/u&gt; compatible with
            versions of Armory before 0.92.
            &lt;br&gt;&lt;br&gt;
            To continue, the other system will need to be upgraded to
            to version 0.92 or later.  If you cannot upgrade the other
            system, you will need to reinstall an older version of Armory
            on this system.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1578"/>
        <source>Do not show this warning again</source>
        <translation>Diese Warnung nicht wieder anzeigen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1612"/>
        <source>No Tools Yet!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1612"/>
        <source>The developer tools are not available yet, but will be added
         soon.  Regardless, developer-mode still offers lots of 
         extra information and functionality that is not available in 
         Standard or Advanced mode.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1652"/>
        <source>Root Pubkey Text Files (*.rootpubkey)</source>
        <translation>Root publik Schlüssel Text Dateien (*.rootpubkey)</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1672"/>
        <source>Aborted</source>
        <translation>Abgebrochen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1672"/>
        <source>
                  No passphrase was selected for the encrypted backup.
                  No backup was created</source>
        <translation>
Es wurde kein Password für das verschlüsselte Backup gewählt.
Es wurde kein Backup erstellt</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1684"/>
        <source>Backup Complete</source>
        <translation>Backup Abgeschlossen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1684"/>
        <source>
         Your wallet was successfully backed up to the following
         location:&lt;br&gt;&lt;br&gt;%1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1747"/>
        <source>Restart Armory</source>
        <translation>Armory neu starten</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1732"/>
        <source>You may have to restart Armory for all aspects of
         the new usermode to go into effect.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1747"/>
        <source>You will have to restart Armory for the new language to go into effect</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1768"/>
        <source>Invalid Date Format</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1768"/>
        <source>The date format you specified was not valid.  Please re-enter
            it using only the strftime symbols shown in the help text.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1795"/>
        <source>Already Open</source>
        <translation>Bereits geöffnet</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1795"/>
        <source>
               Armory is already running!  You can only have one Armory open
               at a time.  Exiting...</source>
        <translation>
Armory läuft bereits! Nur eine Armory kann gleichzeitig geöffnet sein.
Beende...</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1942"/>
        <source>No URL String</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1942"/>
        <source>You have not entered a URL String yet.
               Please go back and enter a URL String.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2043"/>
        <source>clicked</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1953"/>
        <source>entered</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3352"/>
        <source>Offline Mode</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1964"/>
        <source>You %1 on a &quot;bitcoin:&quot; link, but Armory is in
            offline mode, and is not capable of creating transactions. 
            Using links will only work if Armory is connected 
            to the Bitcoin network!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1973"/>
        <source>It looks like you just %1 a &quot;bitcoin:&quot; link, but
                    that link is malformed.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1976"/>
        <source>Please check the source of the link and enter the
                        transaction manually.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1979"/>
        <source>The raw URI string is:

</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1980"/>
        <source>Invalid URI</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1985"/>
        <source>The &quot;bitcoin:&quot; link you just %1
            does not even contain an address!  There is nothing that 
            Armory can do with this link!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1997"/>
        <source>Wrong Network!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="1997"/>
        <source>The address for the &quot;bitcoin:&quot; link you just %1 is
            for the wrong network!  You are on the &lt;b&gt;%2&lt;/b&gt;
            and the address you supplied is for the 
            &lt;b&gt;%3&lt;/b&gt;!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2010"/>
        <source>Unsupported URI</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2010"/>
        <source>The &quot;bitcoin:&quot; link
               you just %1 contains fields that are required but not
               recognized by Armory.  This may be an older version of Armory,
               or the link you %2 on uses an exotic, unsupported format.
               &lt;br&gt;&lt;br&gt;The action cannot be completed.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2027"/>
        <source>You just clicked on a &quot;bitcoin:&quot; link, but Armory is offline
            and cannot send transactions.  Please click the link 
            again when Armory is online.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2270"/>
        <source>All files (*)</source>
        <translation>Alle Dateien (*)</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2413"/>
        <source>Contributor &quot;%1&quot; (%2)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2415"/>
        <source>Contributor %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2418"/>
        <source>Contributor &quot;%1&quot;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2420"/>
        <source>Unknown Contributor</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2485"/>
        <source>Blockchain loaded, wallets sync&apos;d!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2491"/>
        <source>Blockchain Loaded!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2491"/>
        <source>Blockchain loading is complete.
            Your balances and transaction history are now available 
            under the &quot;Transactions&quot; tab.  You can also send and 
            receive bitcoins.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2491"/>
        <source>Do not show me this notification again </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2578"/>
        <source>&lt;b&gt;&lt;font color=&quot;%1&quot;&gt;Maximum Funds:&lt;/font&gt;&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2671"/>
        <source>***MEMPOOL REPLACEABLE*** </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2712"/>
        <source>My Wallets</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2713"/>
        <source>Offline Wallets</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2714"/>
        <source>Other&apos;s wallets</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2715"/>
        <source>All Wallets</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2716"/>
        <source>Custom Filter</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3459"/>
        <source>No Wallets!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2727"/>
        <source>You currently do not have any wallets.  Would you like to
            create one, now?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2740"/>
        <source>Select a Wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2740"/>
        <source>Please select a wallet on the right, to see its properties.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2776"/>
        <source>Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2791"/>
        <source>Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2952"/>
        <source>Already Sweeping</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2952"/>
        <source>You are already in the process of scanning the blockchain for
            the purposes of sweeping other addresses.  You cannot initiate 
            sweeping new addresses until the current operation completes. 
            &lt;br&gt;&lt;br&gt;
            In the future, you may select &quot;Multiple Keys&quot; when entering 
            addresses to sweep.  There is no limit on the number that can be 
            specified, but they must all be entered at once.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3033"/>
        <source>addresses</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3033"/>
        <source>address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2974"/>
        <source>Armory is Offline</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2974"/>
        <source>You have chosen to sweep %1 %2, but Armory is currently
            in offline mode.  The sweep will be performed the next time you 
            go into online mode.  You can initiate online mode (if available) 
            from the dashboard in the main window.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2982"/>
        <source>Armory must scan the global transaction history in order to
            find any bitcoins associated with the %1 you supplied.
            Armory will go into offline mode temporarily while the scan 
            is performed, and you will not have access to balances or be 
            able to create transactions.  The scan may take several minutes.
            &lt;br&gt;&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2991"/>
        <source>There is currently another scan operation being performed.
               Would you like to start the sweep operation after it completes? </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2995"/>
        <source>&lt;b&gt;Would you like to start the scan operation right now?&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="2998"/>
        <source>&lt;br&gt;&lt;br&gt;Clicking &quot;No&quot; will abort the sweep operation</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3000"/>
        <source>Confirm Rescan</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3027"/>
        <source>Nothing to do</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3027"/>
        <source>The private %1 you have provided does not appear to contain
               any funds.  There is nothing to sweep.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3027"/>
        <source>keys</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3027"/>
        <source>key</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3034"/>
        <source>Cannot sweep</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3034"/>
        <source>You cannot sweep the funds from the %1 you specified, because
               the transaction fee would be equal to or greater than the amount 
               swept.
               &lt;br&gt;&lt;br&gt;
               &lt;b&gt;Balance of %2:&lt;/b&gt; %3&lt;br&gt;
               &lt;b&gt;Fee to sweep %4:&lt;/b&gt; %5
               &lt;br&gt;&lt;br&gt;The sweep operation has been canceled.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3050"/>
        <source>multiple addresses</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3052"/>
        <source>address &lt;b&gt;%1&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3054"/>
        <source>wallet &lt;b&gt;&quot;%1&quot;&lt;/b&gt; (%2) </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3097"/>
        <source>Broadcast failed</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3097"/>
        <source>
                  The broadcast process failed unexpectedly. Report this error to
                  the development team if this issue occurs repeatedly
                  </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3119"/>
        <source>Transaction Not Accepted</source>
        <translation>Transaktion nicht angenommen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3119"/>
        <source>
                  The transaction that you just executed failed with 
                  the following error message: &lt;br&gt;&lt;br&gt;
                  &lt;b&gt;%1&lt;/b&gt;
                  &lt;br&gt;&lt;br&gt;
                  
                  
                  &lt;br&gt;&lt;br&gt;On time out errors, the transaction may have actually succeed
                  and this message is displayed prematurely.  To confirm whether the
                  the transaction actually succeeded, you can try this direct link
                  to %2:
                  &lt;br&gt;&lt;br&gt;
                  &lt;a href=&quot;%3&quot;&gt;%4...&lt;/a&gt;
                  &lt;br&gt;&lt;br&gt;
                  If you do not see the
                  transaction on that webpage within one minute, it failed and you
                  should attempt to re-send it.
                  If it &lt;i&gt;does&lt;/i&gt; show up, then you do not need to do anything
                  else -- it will show up in Armory as soon as it receives one
                  confirmation.
                  &lt;br&gt;&lt;br&gt;If the transaction did fail, it is likely because the fee
                  is too low. Try again with a higher fee.

                  If the problem persists, go to &quot;&lt;i&gt;File&lt;/i&gt;&quot; -&gt;
                  &quot;&lt;i&gt;Export Log File&lt;/i&gt;&quot; and then attach it to a support
                  ticket at
                  &lt;a href=&quot;%5&quot;&gt;%5&lt;/a&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3155"/>
        <source>In the future, you may avoid scanning twice by starting Armory in offline mode (--offline), and perform the import before switching to online mode.</source>
        <translation>In Zukunft kann doppeltes Überprüfen vermieden werden, indem Armory im offline Modus (--offline) gestartet wird und das Einlesen erfolgt bevor in den online Modus gewechselt wird.</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3159"/>
        <source>Armory is Busy</source>
        <translation>Armory ist beschäftigt</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3159"/>
        <source>Wallets and addresses cannot be imported while Armory is in the middle of an existing blockchain scan.  Please wait for the scan to finish.  </source>
        <translation>Wallets und Adressen können nicht eingelesen werden während Armory die Blockchain überprüft. Bitte auf Abschluss des Vorgangs warten.</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3173"/>
        <source>Scanning</source>
        <translation>Überprüfe</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3173"/>
        <source>
            Armory is currently in the middle of scanning the blockchain for
            your existing wallets.  New wallets cannot be imported until this
            operation is finished.</source>
        <translation>
Armory ist dabei die Blockchain für die bestehenden Wallets zu
überprüfen. Neue Wallets können erst nach Abschluss der Überprüfung
hinzugefügt werden.</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3193"/>
        <source>Duplicate Wallet!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3193"/>
        <source>
            You selected a wallet that has the same ID as one already 
            in your wallet (%1)!  If you would like to import it anyway,
            please delete the duplicate wallet in Armory, first.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3212"/>
        <source>Be Careful!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3212"/>
        <source>
        &lt;font color=&quot;red&quot;&gt;&lt;b&gt;WARNING:&lt;/b&gt;&lt;/font&gt; You are about to make an
        &lt;u&gt;unencrypted&lt;/u&gt; backup of your wallet.  It is highly recommended
        that you do &lt;u&gt;not&lt;/u&gt; ever save unencrypted wallets to your regular
        hard drive.  This feature is intended for saving to a USB key or
        other removable media.</source>
        <translation>
&lt;font color=&quot;red&quot;&gt;&lt;b&gt;WARNUNG&lt;/b&gt;&lt;/font&gt; Sie sind dabei ein
&lt;u&gt;unverschlüsseltes&lt;/u&gt; Backup ihrer Wallet anzulegen. Es wird
dringend empfohlen &lt;u&gt;niemals&lt;/u&gt; eine unverschlüsselte Wallets
auf ihrer gewöhnlichen Festplatte zu speichern. Diese Funktion ist
für das Speichern auf einen entfernbaren Speicher wie einem
USB-Stick gedacht.</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3229"/>
        <source>Blockchain Not Ready</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3224"/>
        <source>
            The address book is created from transaction data available in 
            the blockchain, which has not finished loading.  The address 
            book will become available when Armory is online.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3229"/>
        <source>
            The address book is created from transaction data available in 
            the blockchain, but Armory is currently offline.  The address 
            book will become available when Armory is online.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3235"/>
        <source>No wallets!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3235"/>
        <source>You have no wallets so
               there is no address book to display.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3287"/>
        <source>Invalid Tx</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3287"/>
        <source>
         The transaction you requested be displayed does not exist in 
         Armory&apos;s database.  This is unusual...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3309"/>
        <source>View Details</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3310"/>
        <source>View on %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3311"/>
        <source>Change Comment</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3312"/>
        <source>Copy Transaction ID</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3313"/>
        <source>Open Relevant Wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3323"/>
        <source>Could not open browser</source>
        <translation>Konnte Browser nicht öffnen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3323"/>
        <source>
               Armory encountered an error opening your web browser.  To view 
               this transaction on blockchain.info, please copy and paste 
               the following URL into your browser: 
               &lt;br&gt;&lt;br&gt;%1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3352"/>
        <source>
           Armory is currently running in offline mode, and has no 
           ability to determine balances or create transactions. 
           &lt;br&gt;&lt;br&gt;
           In order to send coins from this wallet you must use a 
           full copy of this wallet from an online computer, 
           or initiate an &quot;offline transaction&quot; using a watching-only 
           wallet on an online computer.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3362"/>
        <source>Armory Not Ready</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3362"/>
        <source>
           Armory is currently scanning the blockchain to collect 
           the information needed to create transactions.  This typically 
           takes between one and five minutes.  Please wait until your 
           balance appears on the main window, then try again.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3372"/>
        <source>
            You cannot send any bitcoins until you create a wallet and 
            receive some coins.  Would you like to create a wallet?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3403"/>
        <source>You just clicked on a &quot;bitcoin:&quot; link requesting bitcoins
                to be sent to the following address:&lt;br&gt; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3406"/>
        <source>&lt;br&gt;--&lt;b&gt;Address&lt;/b&gt;:<byte value="x9"/>%1 </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3418"/>
        <source>&lt;br&gt;--&lt;b&gt;Amount&lt;/b&gt;:<byte value="x9"/>%1 BTC</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3423"/>
        <source>&lt;br&gt;--&lt;b&gt;Message&lt;/b&gt;:<byte value="x9"/>%1...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3425"/>
        <source>&lt;br&gt;--&lt;b&gt;Message&lt;/b&gt;:<byte value="x9"/>%1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3430"/>
        <source>&lt;br&gt;&lt;br&gt;There is no amount specified in the link, so
            you can decide the amount after selecting a wallet to use 
            for this transaction. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3434"/>
        <source>&lt;br&gt;&lt;br&gt;&lt;b&gt;The specified amount &lt;u&gt;can&lt;/u&gt; be changed&lt;/b&gt; on the
            next screen before hitting the &quot;Send&quot; button. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3440"/>
        <source>
            You just clicked on a &quot;bitcoin:&quot; link to send money, but you 
            urrently have no wallets!  Would you like to create a wallet 
            now?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3459"/>
        <source>
            You have not created any wallets which means there is
            nowhere to store you bitcoins!  Would you like to
            create a wallet now?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3476"/>
        <source>Receive coins with wallet...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3578"/>
        <source>Privacy Warning</source>
        <translation>Datenschutz Warnung</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3542"/>
        <source>
         &lt;b&gt;&lt;u&gt;&lt;font size=3&gt;Wallet Analysis Log Files&lt;/font&gt;&lt;/u&gt;&lt;/b&gt;
         &lt;br&gt;&lt;br&gt;
         The wallet analysis logs contain no personally-identifiable
         information, only a record of errors and inconsistencies
         found in your wallet file.  No private keys or even public
         keys are included.
         &lt;br&gt;&lt;br&gt;

         &lt;b&gt;&lt;u&gt;&lt;font size=3&gt;Regular Log Files&lt;/font&gt;&lt;/u&gt;&lt;/b&gt;
         &lt;br&gt;&lt;br&gt;
         The regular log files do not contain any &lt;u&gt;security&lt;/u&gt;-sensitive
         information, but some users may consider the information to be
         &lt;u&gt;privacy&lt;/u&gt;-sensitive.  The log files may identify some addresses
         and transactions that are related to your wallets.  It is always
         recommended you include your log files with any request to the
         Armory team, unless you are uncomfortable with the privacy
         implications.
         &lt;br&gt;&lt;br&gt;

         &lt;b&gt;&lt;u&gt;&lt;font size=3&gt;Watching-only Wallet&lt;/font&gt;&lt;/u&gt;&lt;/b&gt;
         &lt;br&gt;&lt;br&gt;
         A watching-only wallet is a copy of a regular wallet that does not
         contain any signing keys.  This allows the holder to see the balance
         and transaction history of the wallet, but not spend any of the funds.
         &lt;br&gt;&lt;br&gt;
         You may be requested to submit a watching-only copy of your wallet
         to make sure that there is no
         risk to the security of your funds.  You should not even consider
         sending your
         watching-only wallet unless it was specifically requested by an
         Armory representative.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3578"/>
        <source>

         Armory log files do not contain any &lt;u&gt;security&lt;/u&gt;-sensitive
         information, but some users may consider the information to be
         &lt;u&gt;privacy&lt;/u&gt;-sensitive.  The log files may identify some addresses
         and transactions that are related to your wallets.
         &lt;br&gt;&lt;br&gt;

         &lt;b&gt;No signing-key data is ever written to the log file&lt;/b&gt;.
         Only enough data is there to help the Armory developers
         track down bugs in the software, but it may still be considered
         sensitive information to some users.
         &lt;br&gt;&lt;br&gt;

         Please do not send the log file to the Armory developers if you
         are not comfortable with the privacy implications!  However, if you
         do not send the log file, it may be very difficult or impossible
         for us to help you with your problem. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3649"/>
        <source>Already running!</source>
        <translation>Läuft bereits!</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3649"/>
        <source>
               The Bitcoin software appears to be installed now, but it
               needs to be closed for Armory to work.  Would you like Armory
               to close it for you?</source>
        <translation>
Die Bitcoin Software scheint nun installiert zu sein, jedoch
muss sie geschlossen sein damit Armory funktioniert. So Armory
dies nun tun?</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3657"/>
        <source>Still Missing</source>
        <translation>Fehlt weiterhin</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3657"/>
        <source>
               The Bitcoin software still appears to be missing.  If you
               just installed it, then please adjust your settings to point
               to the installation directory.</source>
        <translation>
Die Bitcoin Software scheint immer noch zu fehlen. Sollte
sie gerade installiert worden sein, muss in den Einstellungen
Der Pfad zum Installationsverzeichnis eingetragen werden.</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3664"/>
        <source>Still Running</source>
        <translation>Läuft immer noch</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3664"/>
        <source>
               Bitcoin Core is still running.  Armory cannot start until
               it is closed.  Do you want Armory to close it for you?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3778"/>
        <source>Close Bitcoin Process</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3779"/>
        <source>Download Bitcoin</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3780"/>
        <source>Open https://bitcoin.org</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3781"/>
        <source>Installation Instructions</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3782"/>
        <source>Change Settings</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3786"/>
        <source>Preparing to shut down..</source>
        <translation>Vorbereitung zum abschalten..</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3818"/>
        <source>Stop existing Bitcoin processes so that Armory can open its own</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3820"/>
        <source>Open browser to Bitcoin webpage to download and install Bitcoin software</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3822"/>
        <source>Instructions for manually installing Bitcoin for operating system</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3824"/>
        <source>Open Armory settings window to change Bitcoin software management</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3828"/>
        <source>
           Will open your default browser to https://bitcoin.org where you can 
           download the latest version of Bitcoin Core, and get other information
           and links about Bitcoin, in general.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3832"/>
        <source>
           Instructions are specific to your operating system and include 
           information to help you verify you are installing the correct software</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3835"/>
        <source>
           Change Bitcoin Core/bitcoind management settings or point Armory to
           a non-standard Bitcoin installation</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3838"/>
        <source>
           Armory has detected a running Bitcoin Core or bitcoind instance and
           will force it to exit</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3844"/>
        <source>This option is not yet available yet!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3851"/>
        <source>Securely download Bitcoin software for Windows %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3853"/>
        <source>
            The downloaded files are cryptographically verified.  
            Using this option will start the installer, you will 
            have to click through it to complete installation.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3869"/>
        <source>
               Download and Install Bitcoin Core for Ubuntu/Debian</source>
        <translation>
Downloade und installiere Bitcoin Core für Ubuntu/Debian</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="3871"/>
        <source>
               Will download and Bitcoin software and cryptographically verify it</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4282"/>
        <source>Not Found</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4282"/>
        <source>
         Attempted to kill the running Bitcoin Core/bitcoind instance,
         but it was not found.  </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4353"/>
        <source>Loading Database Headers</source>
        <translation>Lade Datenbank Header</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4362"/>
        <source>Organizing Blockchain</source>
        <translation>Organisiere Blockchain</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5118"/>
        <source>Scan Transaction History</source>
        <translation>Überprüfe Transaktion Verlauf</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4373"/>
        <source>Reading New Block Headers</source>
        <translation>Lese neue Block Header</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4381"/>
        <source>Building Databases</source>
        <translation>Datenbanken werden aufgebaut</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5052"/>
        <source>Build Databases</source>
        <translation>Datenbanken aufbauen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4391"/>
        <source>Scanning Transaction History</source>
        <translation>Transaktionsverlauf wird überprüft</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4401"/>
        <source>Computing Balances</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4411"/>
        <source>Parsing Tx Hashes</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4425"/>
        <source>Resolving Tx Hashes</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4501"/>
        <source>The following functionality is available while scanning in offline mode:&lt;ul&gt;&lt;li&gt;Create new wallets&lt;/li&gt;&lt;li&gt;Generate receiving addresses for your wallets&lt;/li&gt;&lt;li&gt;Create backups of your wallets (printed or digital)&lt;/li&gt;&lt;li&gt;Change wallet encryption settings&lt;/li&gt;&lt;li&gt;Sign transactions created from an online system&lt;/li&gt;&lt;li&gt;Sign messages&lt;/li&gt;&lt;/ul&gt;&lt;br&gt;&lt;br&gt;&lt;b&gt;NOTE:&lt;/b&gt;  The Bitcoin network &lt;u&gt;will&lt;/u&gt; process transactions to your addresses, even if you are offline.  It is perfectly okay to create and distribute payment addresses while Armory is offline, you just won&apos;t be able to verify those payments until the next time Armory is online.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4517"/>
        <source>The following functionality is available in offline mode:&lt;ul&gt;&lt;li&gt;Create, import or recover wallets&lt;/li&gt;&lt;li&gt;Generate new receiving addresses for your wallets&lt;/li&gt;&lt;li&gt;Create backups of your wallets (printed or digital)&lt;/li&gt;&lt;li&gt;Import private keys to wallets&lt;/li&gt;&lt;li&gt;Change wallet encryption settings&lt;/li&gt;&lt;li&gt;Sign messages&lt;/li&gt;&lt;li&gt;&lt;b&gt;Sign transactions created from an online system&lt;/b&gt;&lt;/li&gt;&lt;/ul&gt;&lt;br&gt;&lt;br&gt;&lt;b&gt;NOTE:&lt;/b&gt;  The Bitcoin network &lt;u&gt;will&lt;/u&gt; process transactions to your addresses, regardless of whether you are online.  It is perfectly okay to create and distribute payment addresses while Armory is offline, you just won&apos;t be able to verify those payments until the next time Armory is online.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4534"/>
        <source>&lt;ul&gt;&lt;li&gt;Create, import or recover Armory wallets&lt;/li&gt;&lt;li&gt;Generate new addresses to receive coins&lt;/li&gt;&lt;li&gt;Send bitcoins to other people&lt;/li&gt;&lt;li&gt;Create one-time backups of your wallets (in printed or digital form)&lt;/li&gt;&lt;li&gt;Click on &quot;bitcoin:&quot; links in your web browser (not supported on all operating systems)&lt;/li&gt;&lt;li&gt;Import private keys to wallets&lt;/li&gt;&lt;li&gt;Monitor payments to watching-only wallets and create unsigned transactions&lt;/li&gt;&lt;li&gt;Sign messages&lt;/li&gt;&lt;li&gt;&lt;b&gt;Create transactions with watching-only wallets, to be signed by an offline wallets&lt;/b&gt;&lt;/li&gt;&lt;/ul&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4560"/>
        <source>
         For more information about Armory, and even Bitcoin itself, you should
         visit the &lt;a href=&quot;https://bitcointalk.org/index.php?board=97.0&quot;&gt;Armory Forum&lt;/a&gt;
<byte value="x9"/> and &lt;a href=&quot;https://bitcoin.org&quot;&gt;Bitcoin.org&lt;/a&gt;.  If
         you are experiencing problems using this software, please visit the
         &lt;a href=&quot;https://bitcointalk.org/index.php?board=97.0&quot;&gt;Armory Forum&lt;/a&gt;. Users
<byte value="x9"/> there will help you with any issues that you have.
         &lt;br&gt;&lt;br&gt;
         &lt;b&gt;&lt;u&gt;IMPORTANT:&lt;/u&gt;&lt;/b&gt; Make a backup of your wallet(s)!  Paper
         backups protect you &lt;i&gt;forever&lt;/i&gt; against forgotten passwords,
         hard-drive failure, and make it easy for your family to recover
         your funds if something terrible happens to you.  &lt;i&gt;Each wallet
         only needs to be backed up once, ever!&lt;/i&gt;  Without it, you are at
         risk of losing all of your Bitcoins!
         &lt;br&gt;&lt;br&gt;</source>
        <translation>
Für weitere Informationen über Armory, und Bitcoin generell, sollten sie
das &lt;a href=&quot;https://bitcointalk.org/index.php?board=97.0&quot;&gt;Armory
Forum&lt;/a&gt; und  &lt;a href=&quot;https://bitcoin.org&quot;&gt;Bitcoin.org&lt;/a&gt; besuchen.
Wenn beim benutzen dieser Software Probleme auftreten besuchen sie
das &lt;a href=&quot;https://bitcointalk.org/index.php?board=97.0&quot;&gt;Armory
Forum&lt;/a&gt;. Nutzer
&lt;byte value=&quot;x9&quot;/&gt; dort werden ihnen weiterhelfen.
&lt;br&gt;&lt;br&gt;
&lt;b&gt;&lt;u&gt;WICHTIG:&lt;/u&gt;&lt;/b&gt; Machen sie ein Backup ihrer Wallet(s)!
Papierbackups schützen sie für &lt;i&gt;immer&lt;/i&gt; gegen vergessene
Passwörter, Festplattenfehler, und machen es für ihre Familie einfach
ihre Gelder zu retten wenn ihnen etwas schreckliches zustösst. &lt;i&gt;Jede
Wallet muss nur ein einziges Mal gesichert werden!&lt;/i&gt; Ohne Backup
laufen sie Gefahr ihre Bitcoins für immer zu verlieren!
&lt;br&gt;&lt;br&gt;</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4576"/>
        <source>&lt;p&gt;&lt;b&gt;You now have access to all the features Armory has to offer!&lt;/b&gt;&lt;br&gt;To see your balances and transaction history, please click on the &quot;Transactions&quot; tab above this text.  &lt;br&gt;Here&apos;s some things you can do with Armory Bitcoin Client:&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4583"/>
        <source>If you experience any performance issues with Armory, please confirm that Bitcoin Core is running and &lt;i&gt;fully synchronized with the Bitcoin network&lt;/i&gt;.  You will see a green checkmark in the bottom right corner of the Bitcoin Core window if it is synchronized.  If not, it is recommended you close Armory and restart it only when you see that checkmark.&lt;br&gt;&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4583"/>
        <source>&lt;b&gt;Please backup your wallets!&lt;/b&gt;  Armory wallets are &quot;deterministic&quot;, meaning they only need to be backed up one time (unless you have imported external addresses/keys). Make a backup and keep it in a safe place!  All funds from Armory-generated addresses will always be recoverable with a paper backup, any time in the future.  Use the &quot;Backup Individual Keys&quot; option for each wallet to backup imported keys.&lt;/p&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4601"/>
        <source>Armory is currently online, but you have requested a sweep operation on one or more private keys.  This requires searching the global transaction history for the available balance of the keys to be swept. &lt;br&gt;&lt;br&gt;Press the button to start the blockchain scan, which will also put Armory into offline mode for a few minutes until the scan operation is complete</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4611"/>
        <source>&lt;b&gt;Wallet balances may be incorrect until the rescan operation is performed!&lt;/b&gt;&lt;br&gt;&lt;br&gt;Armory is currently online, but addresses/keys have been added without rescanning the blockchain.  You may continue using Armory in online mode, but any transactions associated with the new addresses will not appear in the ledger. &lt;br&gt;&lt;br&gt;Pressing the button above will put Armory into offline mode for a few minutes until the scan operation is complete.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4623"/>
        <source>There is no connection to the internet, and there is no other Bitcoin software running.  Most likely you are here because this is a system dedicated to manage offline wallets! &lt;br&gt;&lt;br&gt;&lt;b&gt;If you expected Armory to be in online mode&lt;/b&gt;, please verify your internet connection is active, then restart Armory.  If you think the lack of internet connection is in error (such as if you are using Tor), then you can restart Armory with the &quot;--skip-online-check&quot; option, or change it in the Armory settings.&lt;br&gt;&lt;br&gt;If you do not have Bitcoin Core installed, you can download it from &lt;a href=&quot;https://bitcoin.org&quot;&gt;https://bitcoin.org&lt;/a&gt;.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4646"/>
        <source>You are currently in offline mode, but can switch to online mode by pressing the button above.  However, it is not recommended that you switch until Bitcoin Core/bitcoind is fully synchronized with the bitcoin network.  You will see a green checkmark in the bottom-right corner of the Bitcoin Core window when it is finished.&lt;br&gt;&lt;br&gt;Switching to online mode will give you access to more Armory functionality, including sending and receiving bitcoins and viewing the balances and transaction histories of each of your wallets.&lt;br&gt;&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4660"/>
        <source>You are currently in offline mode because Bitcoin Core is not running.  To switch to online mode, start Bitcoin Core and let it synchronize with the network -- you will see a green checkmark in the bottom-right corner when it is complete.  If Bitcoin Core is already running and you believe the lack of connection is an error (especially if using proxies), please see &lt;a href=&quot;https://bitcointalk.org/index.php?topic=155717.msg1719077#msg1719077&quot;&gt;this link&lt;/a&gt; for options.&lt;br&gt;&lt;br&gt;&lt;b&gt;If you prefer to have Armory do this for you&lt;/b&gt;, then please check &quot;Let Armory run Bitcoin Core in the background&quot; under &quot;File&quot;-&gt;&quot;Settings.&quot;&lt;br&gt;&lt;br&gt;If you already know what you&apos;re doing and simply need to fetch the latest version of Bitcoin Core, you can download it from &lt;a href=&quot;https://bitcoin.org&quot;&gt;https://bitcoin.org&lt;/a&gt;.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4679"/>
        <source>You are currently in offline mode because Armory could not detect an internet connection.  If you think this is in error, then restart Armory using the &quot; --skip-online-check&quot; option, or adjust the Armory settings.  Then restart Armory.&lt;br&gt;&lt;br&gt;If this is intended to be an offline computer, note that it is not necessary to have Bitcoin Core or bitcoind running.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4690"/>
        <source>You are currently in offline mode because Armory could not find the blockchain files produced by Bitcoin Core.  Do you run Bitcoin Core (or bitcoind) from a non-standard directory?   Armory expects to find the blkXXXX.dat files in &lt;br&gt;&lt;br&gt;%1&lt;br&gt;&lt;br&gt; If you know where they are located, please restart Armory using the &quot; --satoshi-datadir=[path]&quot; to notify Armory where to find them.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4700"/>
        <source>Armory was previously online, but the connection to Bitcoin Core/bitcoind was interrupted.  You will not be able to send bitcoins or confirm receipt of bitcoins until the connection is reestablished.  &lt;br&gt;&lt;br&gt;Please check that Bitcoin Core is open and synchronized with the network.  Armory will &lt;i&gt;try to reconnect&lt;/i&gt; automatically when the connection is available again.  If Bitcoin Core is available again, and reconnection does not happen, please restart Armory.&lt;br&gt;&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4710"/>
        <source>Please wait while the global transaction history is scanned. Armory will go into online mode automatically, as soon as the scan is complete.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4715"/>
        <source>Armory is scanning the global transaction history to retrieve information about your wallets.  The &quot;Transactions&quot; tab will be updated with wallet balance and history as soon as the scan is complete.  You may manage your wallets while you wait.&lt;br&gt;&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4727"/>
        <source>It appears you are already running Bitcoin software (Bitcoin Core or bitcoind). Unlike previous versions of Armory, you should &lt;u&gt;not&lt;/u&gt; run this software yourself --  Armory will run it in the background for you.  Either close the Bitcoin application or adjust your settings.  If you change your settings, then please restart Armory.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4736"/>
        <source>&lt;b&gt;Only one more step to getting online with Armory!&lt;/b&gt;   You must install the Bitcoin software from https://bitcoin.org in order for Armory to communicate with the Bitcoin network.  If the Bitcoin software is already installed and/or you would prefer to manage it yourself, please adjust your settings and restart Armory.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4744"/>
        <source>
            &lt;b&gt;To maximize your security, the Bitcoin engine is downloading
            and verifying the global transaction ledger.  &lt;u&gt;This will take
            several hours, but only needs to be done once&lt;/u&gt;!&lt;/b&gt;  It is
            usually best to leave it running over night for this
            initialization process.  Subsequent loads will only take a few
            minutes.
            &lt;br&gt;&lt;br&gt;
            &lt;b&gt;Please Note:&lt;/b&gt; Between Armory and the underlying Bitcoin
            engine, you need to have 40-50 GB of spare disk space available
            to hold the global transaction history.
            &lt;br&gt;&lt;br&gt;
            While you wait, you can manage your wallets.  Make new wallets,
            make digital or paper backups, create Bitcoin addresses to receive
            payments,
            sign messages, and/or import private keys.  You will always
            receive Bitcoin payments regardless of whether you are online,
            but you will have to verify that payment through another service
            until Armory is finished this initialization.</source>
        <translation type="unfinished"/>
    </message>
    <message numerus="yes">
        <location filename="ArmoryQt.py" line="4764"/>
        <source>The software is downloading and processing the latest activity on the network related to your wallet.  This should take only a few minutes.  While you wait, you can manage your wallets.  &lt;br&gt;&lt;br&gt;Now would be a good time to make paper (or digital) backups of your wallet if you have not done so already!  You are protected &lt;i&gt;forever&lt;/i&gt; from hard-drive loss, or forgetting you password. If you do not have a backup, you could lose all of your Bitcoins forever!</source>
        <comment>The software is downloading and processing the latest activity on the network related to your wallets.  This should take only a few minutes.  While you wait, you can manage your wallets.  &lt;br&gt;&lt;br&gt;Now would be a good time to make paper (or digital) backups of your wallets if you have not done so already!  You are protected &lt;i&gt;forever&lt;/i&gt; from hard-drive loss, or forgetting you password. If you do not have a backup, you could lose all of your Bitcoins forever!</comment>
        <translation type="unfinished"><numerusform></numerusform><numerusform></numerusform></translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4788"/>
        <source>Armory&apos;s communication with the Bitcoin network was interrupted. This usually does not happen unless you closed the process that Armory was using to communicate with the network. Armory requires %1 to be running in the background, and this error pops up if it disappears.&lt;br&gt;&lt;br&gt;You may continue in offline mode, or you can close all Bitcoin processes and restart Armory.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4797"/>
        <source>Armory has experienced an issue trying to communicate with the Bitcoin software.  The software is running in the background, but Armory cannot communicate with it through RPC as it expects to be able to.  If you changed any settings in the Bitcoin home directory, please make sure that RPC is enabled and that it is accepting connections from localhost.  &lt;br&gt;&lt;br&gt;If you have not changed anything, please export the log file (from the &quot;File&quot; menu) and open an issue at https://github.com/goatpig/BitcoinArmory/issues</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4808"/>
        <source>Armory does not detect internet access, but it does detect running Bitcoin software.  Armory is in offline-mode. &lt;br&gt;&lt;br&gt;If you are intending to run an offline system, you will not need to have the Bitcoin software installed on the offline computer.  It is only needed for the online computer. If you expected to be online and the absence of internet is an error, please restart Armory using the &quot;--skip-online-check&quot; option.  </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4818"/>
        <source>Armory was started in offline-mode, but detected you are running Bitcoin software.  If you are intending to run an offline system, you will &lt;u&gt;not&lt;/u&gt; need to have the Bitcoin software installed or running on the offline computer.  It is only required for being online. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4825"/>
        <source>The Bitcoin software indicates there is a problem with its databases.  This can occur when Bitcoin Core/bitcoind is upgraded or downgraded, or sometimes just by chance after an unclean shutdown.&lt;br&gt;&lt;br&gt;You can either revert your installed Bitcoin software to the last known working version (but not earlier than version 0.8.1) or delete everything &lt;b&gt;except&lt;/b&gt; &quot;wallet.dat&quot; from the your Bitcoin home directory:&lt;br&gt;&lt;br&gt;&lt;font face=&quot;courier&quot;&gt;&lt;b&gt;%1&lt;/b&gt;&lt;/font&gt;&lt;br&gt;&lt;br&gt;If you choose to delete the contents of the Bitcoin home directory, you will have to do a fresh download of the blockchain again, which will require a few hours the first time.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4849"/>
        <source>
               There was an error starting the underlying Bitcoin engine.
               This should not normally happen.  Usually it occurs when you
               have been using Bitcoin Core prior to using Armory, especially
               if you have upgraded or downgraded Bitcoin Core recently.
               Output from bitcoind:&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4858"/>
        <source>
                  There was an error starting the underlying Bitcoin engine.
                  This should not normally happen.  Usually it occurs when you
                  have been using Bitcoin Core prior to using Armory, especially
                  if you have upgraded or downgraded Bitcoin Core recently.
                  &lt;br&gt;&lt;br&gt;
                  Unfortunately, this error is so strange, Armory does not
                  recognize it.  Please go to &quot;Export Log File&quot; from the &quot;File&quot;
                  menu and submit an issue at https://github.com/goatpig/BitcoinArmory/issues.
                  We apologize for the inconvenience!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5013"/>
        <source>Armory is &lt;u&gt;offline&lt;/u&gt;</source>
        <translation>Armory ist &lt;u&gt;offline&lt;/u&gt;</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="4997"/>
        <source>In case you actually do have internet access, you can use the following links to get Armory installed.  Or change your settings.</source>
        <translation>Falls eine Internetverbindung besteht kann mit dem folgenden Link Armory installiert oder Einstellungen geändert werden.</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5017"/>
        <source>Cannot find Bitcoin Home Directory</source>
        <translation>Bitcoin Home Verzeichnis wurde nicht gefunden</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5025"/>
        <source>Check Again</source>
        <translation>Erneut überprüfen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5047"/>
        <source>Initializing Bitcoin Engine</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5106"/>
        <source>Synchronizing with Network</source>
        <translation>Synchronisiere mit Netzwerk</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5061"/>
        <source>Since version 0.88, Armory runs bitcoind in the background.  You can switch back to the old way in the Settings dialog. </source>
        <translation>Seit Version 0.88 lässt Armory bitcoind im Hintergrund laufen. In den Einstellungen kann dies umgeschaltet werden.</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5079"/>
        <source>Armory is disconnected</source>
        <translation>Armory ist nicht verbunden</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5086"/>
        <source>Armory is online!</source>
        <translation>Armory ist online!</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5114"/>
        <source>Preparing Databases</source>
        <translation>Datenbanken werden vorbereitet</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5281"/>
        <source>&lt;font color=%1&gt;Connected (%2 blocks)&lt;/font&gt; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5287"/>
        <source>Last block received %1 ago</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5293"/>
        <source>&lt;font color=%1&gt;Node offline (%2 blocks)&lt;/font&gt; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5298"/>
        <source>Disconnected from Bitcoin Node, cannot update history &lt;br&gt;&lt;br&gt;Last known block: %1 &lt;br&gt;Received %2 ago</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5436"/>
        <source>BDM error!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5430"/>
        <source>Rebuild and rescan on next start</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5436"/>
        <source>Factory reset on next start</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5442"/>
        <source>BlockDataManager Warning</source>
        <translation>BlockDatenManager Warnung</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5491"/>
        <source>Disconnected</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5491"/>
        <source>Connection to Bitcoin Core client lost!  Armory cannot send nor receive bitcoins until connection is re-established.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5497"/>
        <source>Connected</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5497"/>
        <source>Connection to Bitcoin Core re-established</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="6180"/>
        <source>Database Error</source>
        <translation>Datenbankfehler</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5542"/>
        <source>
                           The DB has returned the following
                           error: &lt;br&gt;&lt;br&gt;
                           &lt;b&gt; %1 &lt;/b&gt; &lt;br&gt;&lt;br&gt;
                           Armory will now shutdown. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5640"/>
        <source>Wallet %1 (%2)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5647"/>
        <source>Lockbox %1 (%2)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5741"/>
        <source>Bitcoins Received!</source>
        <translation>Bitcoins erhalten!</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5760"/>
        <source>Amount:  %1 BTC</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5657"/>
        <source>Recipient:  %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5759"/>
        <source>Bitcoins Sent!</source>
        <translation>Bitcoins versendet!</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5662"/>
        <source>Sender:  %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5713"/>
        <source>Wallet &quot;%1&quot; (%2)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5721"/>
        <source>Lockbox %1-of-%2 &quot;%3&quot; (%4)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5732"/>
        <source>Your bitcoins just did a lap!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5732"/>
        <source>%1 just sent some BTC to itself!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5743"/>
        <source>From:    %2</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5757"/>
        <source>&lt;Multiple Recipients&gt;</source>
        <translation>&lt;Mehrere Empfänger&gt;</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5761"/>
        <source>From:    %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5762"/>
        <source>To:      %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5778"/>
        <source>Minimize or Close</source>
        <translation>Minimieren oder schliessen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5778"/>
        <source>Would you like to minimize Armory to the system tray instead of closing it?</source>
        <translation>Soll Armory im Infobereich minimiert werden, statt geschlossen zu werden?</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5778"/>
        <source>Remember my answer</source>
        <translation>Antwort merken</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5778"/>
        <source>Minimize</source>
        <translation>Minimieren</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5778"/>
        <source>Close</source>
        <translation>Schliessen</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5981"/>
        <source>All wallets are consistent</source>
        <translation>Alle Wallets sind konsitent</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5985"/>
        <source>Consistency Check Failed!</source>
        <translation>Konsistenz Test gescheitert!</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="5997"/>
        <source>
               The wallet analysis tool will become available
               as soon as Armory is done loading.   You can close this
               window and it will reappear when ready.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="6030"/>
        <source>
                  &lt;b&gt;The following dialogs need closed before you can
                  run the wallet analysis tool:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="6054"/>
        <source>Wallet Consistency Check: %p%</source>
        <translation>Wallet Konsistenz Test: %p%</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="6180"/>
        <source>
                           Armory failed to spawn the DB!&lt;br&gt; 
                           Continuing operations in offline mode instead. &lt;br&gt;
                           Refer to the dbLog.txt for more information.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="6298"/>
        <source>Filter:</source>
        <translation>Filter:</translation>
    </message>
    <message>
        <location filename="ArmoryQt.py" line="6316"/>
        <source>Transactions</source>
        <translation>Transaktion</translation>
    </message>
</context>
<context>
    <name>ArmorySplashScreen</name>
    <message>
        <location filename="qtdialogs.py" line="15097"/>
        <source>%1: %2%</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>BareSignatureVerificationWidget</name>
    <message>
        <location filename="toolsDialogs.py" line="265"/>
        <source>Signing Address:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="274"/>
        <source>Signed Message:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="281"/>
        <source>Signature:</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>CardDeckFrame</name>
    <message>
        <location filename="WalletFrames.py" line="499"/>
        <source>Please shuffle a deck of cards and enter the first 40 cards in order below to get at least 192 bits of entropy to properly randomize.

</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="534"/>
        <source>Entropy: %1 bits</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>CoinControlDlg</name>
    <message>
        <location filename="CoinControlUI.py" line="27"/>
        <source>By default, transactions are created using any available coins from all addresses in this wallet.  You can control the source addresses used for this transaction by selecting them below, and unchecking all other addresses.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="CoinControlUI.py" line="33"/>
        <source>Use all selected UTXOs</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="CoinControlUI.py" line="34"/>
        <source>
      By default, Armory will pick a a subset of the UTXOs you pick 
      explicitly through the coin control feature to best suit the
      total spend value of the transaction you are constructing.
      
      &lt;br&gt;&lt;br&gt;
      Checking 'Use all selected UTXOs' forces the construction of a
      transaction that will redeem the exact list of UTXOs you picked 
      instead 
      </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="CoinControlUI.py" line="51"/>
        <source>Accept</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="CoinControlUI.py" line="52"/>
        <source>Cancel</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="CoinControlUI.py" line="75"/>
        <source>Coin Control (Expert)</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>CoinControlTreeModel</name>
    <message>
        <location filename="TreeViewGUI.py" line="671"/>
        <source>Address/ID</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TreeViewGUI.py" line="672"/>
        <source>Comment</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TreeViewGUI.py" line="673"/>
        <source>Balance</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>CoinControlUtxoItem</name>
    <message>
        <location filename="TreeViewGUI.py" line="63"/>
        <source>Block: #%1 | Tx: #%2 | TxOut: #%3</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>CreateTxPage</name>
    <message>
        <location filename="Wizards.py" line="407"/>
        <source>Step 1: Create Transaction</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgAddressBook</name>
    <message>
        <location filename="qtdialogs.py" line="8065"/>
        <source>Select</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8075"/>
        <source>Choose an address from your transaction history,
                            or your own wallet.  If you choose to send to one
                            of your own wallets, the next unused address in
                            that wallet will be used.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8081"/>
        <source>Browse all receiving addresses in
                               this wallet, and all addresses to which this
                               wallet has sent bitcoins.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8085"/>
        <source>&lt;b&gt;Send to Wallet:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8086"/>
        <source>&lt;b&gt;Send to Address:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8137"/>
        <source>Receiving (Mine)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8139"/>
        <source>Sending (Other&apos;s)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8174"/>
        <source>The next unused address in that wallet will be calculated and selected. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8176"/>
        <source>Addresses that are in other wallets you own are &lt;b&gt;not showns&lt;/b&gt;.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8181"/>
        <source>No Wallet Selected</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8182"/>
        <source>Use Bare Multi-Sig (No P2SH)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8184"/>
        <source>
         EXPERT OPTION:  Do not check this box unless you know what it means
                         and you need it!  Forces Armory to exposes public
                         keys to the blockchain before the funds are spent.
                         This is only needed for very specific use cases,
                         and otherwise creates blockchain bloat.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8193"/>
        <source>No Address Selected</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8196"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8202"/>
        <source>&lt;&lt;&lt; Go Back</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8309"/>
        <source>None Selected</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8397"/>
        <source>%1 Wallet: %2</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8438"/>
        <source>%1 Address: %2...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8486"/>
        <source>
               Bare multi-sig is not available for M-of-N lockboxes on the
               main Bitcoin network with N higher than 3.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8535"/>
        <source>P2SH Not Allowed</source>
        <translation>P2SH nicht erlaubt</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8535"/>
        <source>
               This operation requires a public key, but you selected a
               P2SH address which does not have a public key (these addresses
               start with &quot;2&quot; or &quot;3&quot;).  Please select a different address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8569"/>
        <source>No Public Key</source>
        <translation>Kein öffentlicher Schlüssel</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8569"/>
        <source>
            This operation requires a full public key, not just an address.
            Unfortunately, Armory cannot find the public key for the address
            you selected.  In general public keys will only be available
            for addresses in your wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8620"/>
        <source>Copy Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8621"/>
        <source>Copy Hash160 (hex)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8622"/>
        <source>Copy Comment</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgAddressInfo</name>
    <message>
        <location filename="qtdialogs.py" line="3564"/>
        <source>Information for address:  %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3576"/>
        <source>This is the computer-readable form of the address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3578"/>
        <source>&lt;b&gt;Public Key Hash&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3584"/>
        <source>%1 (Network: %2 / Checksum: %3)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3591"/>
        <source>&lt;b&gt;Wallet:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3596"/>
        <source>&lt;b&gt;Address:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3601"/>
        <source>
         Address type is either &lt;i&gt;Imported&lt;/i&gt; or &lt;i&gt;Permanent&lt;/i&gt;.
         &lt;i&gt;Permanent&lt;/i&gt;
         addresses are part of the base wallet, and are protected by printed
         paper backups, regardless of when the backup was performed.
         Imported addresses are only protected by digital backups, or manually
         printing the individual keys list, and only if the wallet was backed up
         &lt;i&gt;after&lt;/i&gt; the keys were imported.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3610"/>
        <source>&lt;b&gt;Address Type:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3624"/>
        <source>Imported</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3614"/>
        <source>Permanent</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3618"/>
        <source>The index of this address within the wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3620"/>
        <source>&lt;b&gt;Index:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3629"/>
        <source>
            This is the current &lt;i&gt;spendable&lt;/i&gt; balance of this address,
            not including zero-confirmation transactions from others.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3632"/>
        <source>&lt;b&gt;Current Balance&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3648"/>
        <source>&lt;b&gt;Comment:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3660"/>
        <source>The total number of transactions in which this address was involved</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3662"/>
        <source>&lt;b&gt;Transaction Count:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3682"/>
        <source>&lt;font size=2&gt;Double-click to expand&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3726"/>
        <source>
            Unlike the wallet-level ledger, this table shows every
            transaction &lt;i&gt;input&lt;/i&gt; and &lt;i&gt;output&lt;/i&gt; as a separate entry.
            Therefore, there may be multiple entries for a single transaction,
            which will happen if money was sent-to-self (explicitly, or as
            the change-back-to-self address).</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3732"/>
        <source>All Address Activity:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3741"/>
        <source>Copy Address to Clipboard</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3742"/>
        <source>View Address Keys</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3744"/>
        <source>Delete Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3761"/>
        <source>
         NOTE:  The ledger shows each transaction &lt;i&gt;&lt;b&gt;input&lt;/b&gt;&lt;/i&gt; and
         &lt;i&gt;&lt;b&gt;output&lt;/b&gt;&lt;/i&gt; for this address.  There are typically many
         inputs and outputs for each transaction, therefore the entries
         represent only partial transactions.  Do not worry if these entries
         do not look familiar.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3781"/>
        <source>Available Actions:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3784"/>
        <source>&lt;&lt;&lt; Go Back</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3788"/>
        <source>Address Information</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3797"/>
        <source>&lt;i&gt;Copied!&lt;/i&gt;</source>
        <translation>&lt;i&gt;Kopiert!&lt;/i&gt;</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3806"/>
        <source>Wallet is Locked</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3806"/>
        <source>Key information will not include the private key data.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgBackupCenter</name>
    <message>
        <location filename="qtdialogs.py" line="11163"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11174"/>
        <source>Backup Center</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgBadConnection</name>
    <message>
        <location filename="qtdialogs.py" line="7534"/>
        <source>
            Armory was not able to detect an internet connection, so Armory
            will operate in &quot;Offline&quot; mode.  In this mode, only wallet
            -management and unsigned-transaction functionality will be available.
            &lt;br&gt;&lt;br&gt;
            If this is an error, please check your internet connection and
            restart Armory.&lt;br&gt;&lt;br&gt;Would you like to continue in &quot;Offline&quot; mode? </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7542"/>
        <source>
            Armory was not able to detect the presence of Bitcoin Core or bitcoind
            client software (available at https://bitcoin.org).  Please make sure that
            the one of those programs is... &lt;br&gt;
            &lt;br&gt;&lt;b&gt;(1)&lt;/b&gt; ...open and connected to the network
            &lt;br&gt;&lt;b&gt;(2)&lt;/b&gt; ...on the same network as Armory (main-network or test-network)
            &lt;br&gt;&lt;b&gt;(3)&lt;/b&gt; ...synchronized with the blockchain before
            starting Armory&lt;br&gt;&lt;br&gt;Without the Bitcoin Core or bitcoind open, you will only
            be able to run Armory in &quot;Offline&quot; mode, which will not have access
            to new blockchain data, and you will not be able to send outgoing
            transactions&lt;br&gt;&lt;br&gt;If you do not want to be in &quot;Offline&quot; mode, please
            restart Armory after one of these programs is open and synchronized with
            the network</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7567"/>
        <source>Continue in Offline Mode</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7568"/>
        <source>Close Armory</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7580"/>
        <source>Network not available</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgBroadcastBlindTx</name>
    <message>
        <location filename="qtdialogs.py" line="14855"/>
        <source>
         Copy a raw, hex-encoded transaction below to have Armory
         broadcast it to the Bitcoin network.  This function is
         provided as a convenience to expert users, and carries
         no guarantees of usefulness.
         &lt;br&gt;&lt;br&gt;
         Specifically, be aware of the following limitations of
         this broadcast function:
         &lt;ul&gt;
            &lt;li&gt;The transaction will be &quot;broadcast&quot; by sending it
                to the connected Bitcon Core instance which will
                forward it to the rest of the Bitcoin network.
                However, if the transaction is non-standard or
                does not satisfy standard fee rules, Bitcoin Core
                &lt;u&gt;will&lt;/u&gt; drop it and it
                will never be seen by the Bitcoin network.
            &lt;/li&gt;
            &lt;li&gt;There will be no feedback as to whether the
                transaction succeeded.  You will have to verify the
                success of this operation via other means.
                However, if the transaction sends
                funds directly to or from an address in one of your
                wallets, it will still generate a notification and show
                up in your transaction history for that wallet.
            &lt;/li&gt;
         &lt;/ul&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14889"/>
        <source>Parsed Transaction:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14899"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14900"/>
        <source>Broadcast</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14916"/>
        <source>Broadcast Raw Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14942"/>
        <source>&lt;font color=&quot;%1&quot;&gt;&lt;b&gt;Raw transaction
            is invalid!&lt;/font&gt;&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14969"/>
        <source>Broadcast!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14969"/>
        <source>
         Your transaction was successfully sent to the local Bitcoin
         Core instance, though there is no guarantees that it was
         forwarded to the rest of the network.   On testnet, just about
         every valid transaction will successfully propagate.  On the
         main Bitcoin network, this will fail unless it was a standard
         transaction type.

         The transaction
         had the following hash:
         &lt;br&gt;&lt;br&gt;
         %1
         &lt;br&gt;&lt;br&gt;
         You can check whether it was seen by other nodes on the network
         with the link below:
         &lt;br&gt;&lt;br&gt;
         &lt;a href=&quot;%2&quot;&gt;%3&lt;/a&gt;</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgChangeLabels</name>
    <message>
        <location filename="qtdialogs.py" line="1434"/>
        <source>Wallet &amp;name:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1440"/>
        <source>Wallet &amp;description:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1460"/>
        <source>Wallet Descriptions</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1470"/>
        <source>Empty Name</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1470"/>
        <source>All wallets must have a name. </source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgChangePassphrase</name>
    <message>
        <location filename="qtdialogs.py" line="1263"/>
        <source>Please enter an passphrase for wallet encryption.


                              A good passphrase consists of at least 8 or more

                              random letters, or 5 or more random words.
</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1269"/>
        <source>Change your wallet encryption passphrase</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1274"/>
        <source>Current Passphrase:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1280"/>
        <source>New Passphrase:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1285"/>
        <source>Again:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1300"/>
        <source>Disable encryption for this wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1307"/>
        <source>Accept</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1308"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1315"/>
        <source>Set Encryption Passphrase</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1317"/>
        <source>Change Encryption Passphrase</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1349"/>
        <source>&lt;font color=%1&gt;&lt;b&gt;Passphrase is non-ASCII!&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1352"/>
        <source>&lt;font color=%1&gt;&lt;b&gt;Passphrases do not match!&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1355"/>
        <source>&lt;font color=%1&gt;&lt;b&gt;Passphrase is too short!&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1357"/>
        <source>&lt;font color=%1&gt;&lt;b&gt;Passphrases match!&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1369"/>
        <source>Invalid Passphrase</source>
        <translation>Ungültiges Passwort</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1369"/>
        <source>You entered your confirmation passphrase incorrectly!</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgConfirmBulkImport</name>
    <message>
        <location filename="qtdialogs.py" line="3447"/>
        <source>No Addresses to Import</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3447"/>
        <source>
           There are no addresses to import!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3452"/>
        <source>a new wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3455"/>
        <source>wallet, &lt;b&gt;%1&lt;/b&gt; (%2)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3456"/>
        <source>
         'You are about to import &lt;b&gt;%1&lt;/b&gt; addresses into %2.&lt;br&gt;&lt;br&gt; '
         &apos;The following is a list of addresses to be imported:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3471"/>
        <source>Import</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3472"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3484"/>
        <source>Confirm Import</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgConfirmSend</name>
    <message>
        <location filename="qtdialogs.py" line="4900"/>
        <source> To see complete transaction details
                             &lt;a href=&quot;None&quot;&gt;click here&lt;/a&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4909"/>
        <source>
         This transaction will spend &lt;b&gt;%1 BTC&lt;/b&gt; from
         &lt;font color=&quot;%2&quot;&gt;Wallet &quot;&lt;b&gt;%3&lt;/b&gt;&quot; (%4)&lt;/font&gt; to the following
         recipients:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4915"/>
        <source>
            &lt;font size=3&gt;* Starred
            outputs are going to the same wallet from which they came
            and do not affect the wallet's final balance.
            The total balance of the wallet will actually only decrease
            &lt;b&gt;%1 BTC&lt;/b&gt; as a result of this transaction.  %2&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4963"/>
        <source>Send</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4964"/>
        <source>Are you sure you want to execute this transaction?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4966"/>
        <source>Continue</source>
        <translation>Fortfahren</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4967"/>
        <source>Does the above look correct?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4969"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4993"/>
        <source>Confirm Transaction</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgCorruptWallet</name>
    <message>
        <location filename="qtdialogs.py" line="14309"/>
        <source>Wallet Consistency Check Failed!</source>
        <translation>Wallet Konsistenz Tests gescheitert!</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14311"/>
        <source>Perform Wallet Consistency Check</source>
        <translation>Wallet Konsistenz Test ausführen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14313"/>
        <source>
         &lt;font color=&quot;%1&quot; size=5&gt;&lt;b&gt;&lt;u&gt;%2&lt;/u&gt;&lt;/b&gt;&lt;/font&gt;
         &lt;br&gt;&lt;br&gt;
         Armory software now detects and prevents certain kinds of
         hardware errors that could lead to problems with your wallet.
         &lt;br&gt; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14324"/>
        <source>
            Armory has detected that wallet file &lt;b&gt;Wallet &quot;%1&quot; (%2)&lt;/b&gt;
            is inconsistent and should be further analyzed to ensure that your
            funds are protected.
            &lt;br&gt;&lt;br&gt;
            &lt;font color=&quot;%3&quot;&gt;This error will pop up every time you start
            Armory until the wallet has been analyzed and fixed!&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14332"/>
        <source>
            Armory will perform a consistency check on &lt;b&gt;Wallet &quot;%1&quot; (%2)&lt;/b&gt;
            and determine if any further action is required to keep your funds
            protected.  This check is normally performed on startup on all
            your wallets, but you can click below to force another
            check.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14363"/>
        <source>Hide</source>
        <translation>Verstecken</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14364"/>
        <source>Run Analysis and Recovery Tool</source>
        <translation>Starte das Analyse- und Wiederherstellungswerkzeug</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14374"/>
        <source>
         &lt;u&gt;Your wallets will be ready to fix once the scan is over&lt;/u&gt;&lt;br&gt;
         You can hide this window until then&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14396"/>
        <source>Wallet Error</source>
        <translation>Walletfehler</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14422"/>
        <source>Close</source>
        <translation>Schliessen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14511"/>
        <source>Continue</source>
        <translation>Fortfahren</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14526"/>
        <source>
            &lt;font size=4 color=&quot;%1&quot;&gt;&lt;b&gt;Failed to fix wallets!&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message numerus="yes">
        <location filename="qtdialogs.py" line="14530"/>
        <source>
            &lt;font size=4 color=&quot;%1&quot;&gt;&lt;b&gt;Wallet consistent, nothing to
            fix.&lt;/b&gt;&lt;/font&gt;</source>
        <comment>
            &lt;font size=4 color=&quot;%1&quot;&gt;&lt;b&gt;Wallets consistent, nothing to
            fix.&lt;/b&gt;&lt;/font&gt;</comment>
        <translation type="unfinished"><numerusform></numerusform><numerusform></numerusform></translation>
    </message>
    <message numerus="yes">
        <location filename="qtdialogs.py" line="14537"/>
        <source>Wallet consistent!</source>
        <comment>Wallets consistent!</comment>
        <translation type="unfinished"><numerusform></numerusform><numerusform></numerusform></translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14542"/>
        <source>
               &lt;font color=&quot;%1&quot;&gt;&lt;b&gt;
               &lt;font size=4&gt;&lt;b&gt;&lt;u&gt;There may still be issues with your
               wallet!&lt;/u&gt;&lt;/b&gt;&lt;/font&gt;
               &lt;br&gt;
               It is important that you send us the recovery logs
               and an email address so the Armory team can check for
               further risk to your funds!&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14552"/>
        <source>&lt;h2 style=&quot;color: red;&quot;&gt;                                     Consistency check failed! &lt;/h2&gt;</source>
        <translation>&lt;h2 style=&quot;color:red;&quot;&gt;Konsistenz Test gescheitert!&lt;/h2&gt;</translation>
    </message>
</context>
<context>
    <name>DlgCreatePromNote</name>
    <message>
        <location filename="MultiSigDialogs.py" line="3127"/>
        <source>
         &lt;font color=&quot;%1&quot; size=4&gt;&lt;b&gt;Create Simulfunding Promissory Note
         &lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3132"/>
        <source>
         Use this form to create a
         &quot;promissory note&quot; which can be combined with notes from other 
         parties to fund an address or lockbox simultaneously
         (&lt;i&gt;&quot;simulfunding&quot;&lt;/i&gt;).  This funding
         transaction will not be valid until all promissory notes are 
         merged into a single transaction, then all funding parties 
         will review and sign it.  
         &lt;br&gt;&lt;br&gt;
         If this lockbox is being funded by only one party, using this
         interface is unnecessary.  Have the funding party send Bitcoins 
         to the destination address or lockbox in the normal way.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3145"/>
        <source>
         &lt;b&gt;NOTE:&lt;/b&gt; At the moment, simulfunding is restricted to using
         single-signature wallets/addresses for funding.    More
         complex simulfunding transactions will be possible in a future 
         version of Armory.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3167"/>
        <source>Address:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3168"/>
        <source>Amount:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3169"/>
        <source>Add fee:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3171"/>
        <source>BTC</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3205"/>
        <source>
         This label will be attached to the promissory note to help identify
         who is committing these funds.  If you do not fill this in, each
         other party signing will see &lt;i&gt;[[Unknown Signer]]&lt;/i&gt; for the ID.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3213"/>
        <source>Source of Funding</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3220"/>
        <source>Funding Destination</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3243"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3244"/>
        <source>Continue</source>
        <translation>Fortfahren</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3309"/>
        <source>Blockchain Not Available</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3309"/>
        <source>
            The blockchain has become unavailable since you opened this
            window.  Creation of the promissory note cannot continue.  If 
            you think you should be online, please try again in a minute,
            or after restarting Armory</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3321"/>
        <source>Lockbox Selected</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3321"/>
        <source>
            Currently, Armory does not implement simulfunding with lockbox
            inputs.  Please choose a regular wallet as your input</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3328"/>
        <source>No Wallet Selected</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3328"/>
        <source>
            The wallet selected is not available.  Select another wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3338"/>
        <source>Zero Amount</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3338"/>
        <source>
               You cannot promise 0 BTC.   &lt;br&gt;Please enter 
               a positive amount.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3343"/>
        <source>Negative Value</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3369"/>
        <source>
            You have specified a negative amount. &lt;br&gt;Only
            positive values are allowed!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3374"/>
        <source>Too much precision</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3374"/>
        <source>
            Bitcoins can only be specified down to 8 decimal places. 
            The smallest value that can be sent is  0.0000 0001 BTC. 
            Please enter a new amount</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3380"/>
        <source>Missing amount</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3354"/>
        <source>
            You did not specify an amount to promise!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3358"/>
        <source>Invalid Value String</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3384"/>
        <source>
            The amount you specified is invalid (%1).</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3369"/>
        <source>Negative Fee</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3380"/>
        <source>
            &apos;You did not specify an amount to promise!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3384"/>
        <source>Invalid Fee String</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3394"/>
        <source>Not enough funds!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3394"/>
        <source>
            You specified &lt;b&gt;%1&lt;/b&gt; BTC (amount + fee), but the selected wallet
            only has &lt;b&gt;%2&lt;/b&gt; BTC spendable.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3404"/>
        <source>Coin Selection Error</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3404"/>
        <source>
            There was an error constructing your transaction, due to a 
            quirk in the way Bitcoin transactions work.  If you see this
            error more than once, try sending your BTC in two or more 
            separate transactions.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3436"/>
        <source>Transaction Not Found</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3436"/>
        <source>
               There was an error creating the promissory note -- the selected
               coins were not found in the blockchain.  Please go to 
               &quot;&lt;i&gt;Help&lt;/i&gt;&quot;â&quot;&lt;i&gt;Submit Bug Report&lt;/i&gt;&quot; from 
               the main window and submit your log files so the Armory team
               can review this error.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3460"/>
        <source>Export Promissory Note</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3461"/>
        <source>
            The text below includes all the data needed to represent your
            contribution to a simulfunding transaction.  Your money cannot move
            because you have not signed anything, yet.  Once all promissory
            notes are collected, you will be able to review the entire funding 
            transaction before signing.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgDispTxInfo</name>
    <message>
        <location filename="qtdialogs.py" line="5741"/>
        <source>Sent-to-Self</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5758"/>
        <source>Received</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5762"/>
        <source>Sent</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5779"/>
        <source>Transaction Information:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5799"/>
        <source>Unique identifier for this transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5800"/>
        <source>Transaction ID</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5805"/>
        <source>[[ Transaction ID cannot be determined without all signatures ]]</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5815"/>
        <source>&lt;font color=&quot;gray&quot;&gt;
               [[ Transaction ID cannot be determined without all signatures ]]
               &lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5829"/>
        <source>Bitcoin Protocol Version Number</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5830"/>
        <source>Tx Version:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5834"/>
        <source>The time at which this transaction becomes valid.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5836"/>
        <source>Lock-Time:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5838"/>
        <source>Immediate (0)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5840"/>
        <source>Block %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5847"/>
        <source>Comment stored for this transaction in this wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5848"/>
        <source>User Comment:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5852"/>
        <source>&lt;font color=&quot;gray&quot;&gt;[None]&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5858"/>
        <source>The time that you computer first saw this transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5861"/>
        <source>All transactions are eventually included in a &quot;block.&quot;  The
                  time shown here is the time that the block entered the &quot;blockchain.&quot;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5871"/>
        <source>This transaction has not yet been included in a block.
                  It usually takes 5-20 minutes for a transaction to get
                  included in a block after the user hits the &quot;Send&quot; button.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5883"/>
        <source>Every transaction is eventually included in a &quot;block&quot; which
                  is where the transaction is permanently recorded.  A new block
                  is produced approximately every 10 minutes.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5892"/>
        <source>The number of blocks that have been produced since
                     this transaction entered the blockchain.  A transaction
                     with 6 or more confirmations is nearly impossible to reverse.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5896"/>
        <source>Confirmations:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5902"/>
        <source>This transaction can be replaced by another transaction that
               spends the same inputs if the replacement transaction has
               a higher fee.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5906"/>
        <source>Mempool Replaceable: </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5915"/>
        <source>Most transactions have at least a recipient output and a
               returned-change output.  You do not have enough information
               to determine which is which, and so this fields shows the sum
               of &lt;b&gt;all&lt;/b&gt; outputs.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5920"/>
        <source>Sum of Outputs:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5924"/>
        <source>Bitcoins were either sent or received, or sent-to-self</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5930"/>
        <source>The value shown here is the net effect on your
               wallet, including transaction fee.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5944"/>
        <source>Transaction fees go to users supplying the Bitcoin network with
            computing power for processing transactions and maintaining security.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5976"/>
        <source>All outputs of the transaction &lt;b&gt;excluding&lt;/b&gt; change-
                  back-to-sender outputs.  If this list does not look
                  correct, it is possible that the change-output was
                  detected incorrectly -- please check the complete
                  input/output list below.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5982"/>
        <source>Recipients:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5996"/>
        <source>[%1 more recipients]</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6110"/>
        <source>Transaction Inputs (Sending addresses):</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6111"/>
        <source>All transactions require previous transaction outputs as
                  inputs.  </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6114"/>
        <source>&lt;b&gt;Since the blockchain is not available, not all input
                      information is available&lt;/b&gt;.  You need to view this
                      transaction on a system with an internet connection
                      (and blockchain) if you want to see the complete information.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6119"/>
        <source>Each input is like an X amount dollar bill.  Usually there are more inputs
                      than necessary for the transaction, and there will be an extra
                      output returning change to the sender</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6124"/>
        <source>Transaction Outputs (Receiving addresses):</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6125"/>
        <source>Shows &lt;b&gt;all&lt;/b&gt; outputs, including other recipients
                  of the same transaction, and change-back-to-sender outputs
                  (change outputs are displayed in light gray).</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6130"/>
        <source>Some outputs might be &quot;change.&quot;
         </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6149"/>
        <source>Copy Raw Tx (Hex)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6151"/>
        <source>OK</source>
        <translation>OK</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6188"/>
        <source>Transaction Info</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6197"/>
        <source>&lt;&lt;&lt; Less Info</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6207"/>
        <source>Advanced &gt;&gt;&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6218"/>
        <source>TxIn Script:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6225"/>
        <source>TxOut Script:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6264"/>
        <source>&lt;i&gt;Copied to Clipboard!&lt;/i&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6294"/>
        <source>Copy Sender Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6330"/>
        <source>Copy Wallet ID</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6331"/>
        <source>Copy Amount</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6297"/>
        <source>More Info</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6329"/>
        <source>Copy Recipient Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6332"/>
        <source>Copy Raw Script</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgDisplayTxIn</name>
    <message>
        <location filename="qtdialogs.py" line="6358"/>
        <source>&lt;center&gt;&lt;u&gt;&lt;b&gt;TxIn Information&lt;/b&gt;&lt;/u&gt;&lt;/center&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6383"/>
        <source>[[Cannot determine from TxIn Script]]</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6388"/>
        <source>Wallet &quot;%1&quot; (%2)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6392"/>
        <source>Lockbox %1-of-%2 &quot;%3&quot; (%4)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6397"/>
        <source>&lt;font size=4&gt;&lt;u&gt;&lt;b&gt;Information on TxIn&lt;/b&gt;&lt;/u&gt;&lt;/font&gt;:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6398"/>
        <source>   &lt;b&gt;TxIn Index:&lt;/b&gt;         %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6399"/>
        <source>   &lt;b&gt;TxIn Spending:&lt;/b&gt;      %1:%2</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6400"/>
        <source>   &lt;b&gt;TxIn Sequence&lt;/b&gt;:      0x%1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6402"/>
        <source>   &lt;b&gt;TxIn Script Type&lt;/b&gt;:   %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6403"/>
        <source>   &lt;b&gt;TxIn Source&lt;/b&gt;:        %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6405"/>
        <source>   &lt;b&gt;TxIn Wallet&lt;/b&gt;:        %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6406"/>
        <source>   &lt;b&gt;TxIn Script&lt;/b&gt;:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6434"/>
        <source>&lt;font size=4&gt;&lt;u&gt;&lt;b&gt;Information on TxOut being spent by this TxIn&lt;/b&gt;&lt;/u&gt;&lt;/font&gt;:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6435"/>
        <source>   &lt;b&gt;Tx Hash:&lt;/b&gt;            %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6436"/>
        <source>   &lt;b&gt;Tx Out Index:&lt;/b&gt;       %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6437"/>
        <source>   &lt;b&gt;Tx in Block#:&lt;/b&gt;       %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6438"/>
        <source>   &lt;b&gt;TxOut Value:&lt;/b&gt;        %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6439"/>
        <source>   &lt;b&gt;TxOut Script Type:&lt;/b&gt;  %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6440"/>
        <source>   &lt;b&gt;TxOut Address:&lt;/b&gt;      %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6442"/>
        <source>   &lt;b&gt;TxOut Wallet:&lt;/b&gt;       %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6443"/>
        <source>   &lt;b&gt;TxOUt Script:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6450"/>
        <source>Ok</source>
        <translation>Ok</translation>
    </message>
</context>
<context>
    <name>DlgDisplayTxOut</name>
    <message>
        <location filename="qtdialogs.py" line="6468"/>
        <source>&lt;center&gt;&lt;u&gt;&lt;b&gt;TxOut Information&lt;/b&gt;&lt;/u&gt;&lt;/center&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6504"/>
        <source>&lt;font size=4&gt;&lt;u&gt;&lt;b&gt;Information on TxOut&lt;/b&gt;&lt;/u&gt;&lt;/font&gt;:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6505"/>
        <source>   &lt;b&gt;Tx Out Index:&lt;/b&gt;       %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6506"/>
        <source>   &lt;b&gt;TxOut Value:&lt;/b&gt;        %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6507"/>
        <source>   &lt;b&gt;TxOut Script Type:&lt;/b&gt;  %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6508"/>
        <source>   &lt;b&gt;TxOut Address:&lt;/b&gt;      %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6510"/>
        <source>   &lt;b&gt;TxOut Wallet:&lt;/b&gt;       %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6512"/>
        <source>   &lt;b&gt;TxOut Wallet:&lt;/b&gt;       [[Unrelated to any loaded wallets]]</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6513"/>
        <source>   &lt;b&gt;TxOut Script:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6520"/>
        <source>Ok</source>
        <translation>Ok</translation>
    </message>
</context>
<context>
    <name>DlgDuplicateAddr</name>
    <message>
        <location filename="qtdialogs.py" line="3498"/>
        <source>No Addresses to Import</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3498"/>
        <source>There are no addresses to import!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3502"/>
        <source>
         &lt;font color=%1&gt;Duplicate addresses detected!&lt;/font&gt; The following
         addresses already exist in other Armory wallets:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3515"/>
        <source>
         Duplicate addresses cannot be imported.  If you continue,
         the addresses above will be ignored, and only new addresses
         will be imported to this wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3521"/>
        <source>Continue</source>
        <translation>Fortfahren</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3522"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3535"/>
        <source>Duplicate Addresses</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgECDSACalc</name>
    <message>
        <location filename="qtdialogs.py" line="7809"/>
        <source>Multiply Scalars (mod n)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7810"/>
        <source>Scalar Multiply EC Point</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7811"/>
        <source>Add EC Points</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7926"/>
        <source>Clear</source>
        <translation>Leeren</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7913"/>
        <source>
         Use this form to perform Bitcoin elliptic curve calculations.  All
         operations are performed on the secp256k1 elliptic curve, which is
         the one used for Bitcoin.
         Supply all values as 32-byte, big-endian, hex-encoded integers.
         &lt;br&gt;&lt;br&gt;
         The following is the secp256k1 generator point coordinates (G): &lt;br&gt;
            &lt;b&gt;G&lt;/b&gt;&lt;sub&gt;x&lt;/sub&gt;: %1 &lt;br&gt;
            &lt;b&gt;G&lt;/b&gt;&lt;sub&gt;y&lt;/sub&gt;: %2</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7930"/>
        <source>&lt;&lt;&lt; Go Back</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7946"/>
        <source>ECDSA Calculator</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7956"/>
        <source>Bad Input</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7956"/>
        <source>Value &quot;%1&quot; is invalid.  Make sure the value is specified in
            hex, big-endian</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8010"/>
        <source>Invalid EC Point</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7982"/>
        <source>The point you specified (&lt;b&gt;B&lt;/b&gt;) is not on the
            elliptic curve used in Bitcoin (secp256k1).</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8004"/>
        <source>The point you specified (&lt;b&gt;A&lt;/b&gt;) is not on the
            elliptic curve used in Bitcoin (secp256k1).</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8010"/>
        <source>'The point you specified (&lt;b&gt;B&lt;/b&gt;) is not on the
            elliptic curve used in Bitcoin (secp256k1).</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgEULA</name>
    <message>
        <location filename="qtdialogs.py" line="3956"/>
        <source>I agree to all the terms of the license above</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3958"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3959"/>
        <source>Accept</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3967"/>
        <source>
         &lt;b&gt;Armory Bitcoin Client is licensed in part under the
         &lt;i&gt;Affero General Public License, Version 3 (AGPLv3)&lt;/i&gt;
         and in part under the &lt;i&gt;MIT License&lt;/i&gt;&lt;/b&gt;
         &lt;br&gt;&lt;br&gt;
         Additionally, as a condition of receiving this software
         for free, you accept all risks associated with using it
         and the developers of Armory will not be held liable for any
         loss of money or bitcoins due to software defects.
         &lt;br&gt;&lt;br&gt;
         &lt;b&gt;Please read the full terms of the license and indicate your
         agreement with its terms.&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3988"/>
        <source>Armory License Agreement</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgEnterOneFrag</name>
    <message>
        <location filename="qtdialogs.py" line="13462"/>
        <source> You have entered fragments %1, so far.  </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13464"/>
        <source>
         &lt;b&gt;&lt;u&gt;Enter Another Fragment...&lt;/u&gt;&lt;/b&gt; &lt;br&gt;&lt;br&gt; %1
         The fragments can be entered in any order, as long as you provide
         enough of them to restore the wallet.  If any fragments use a
         SecurePrintâ¢ code, please enter it once on the
         previous window, and it will be applied to all fragments that
         require it.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13514"/>
        <source>&lt;b&gt;Backup Type:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13546"/>
        <source>SecurePrintâ¢ Code:</source>
        <translation>SicherheitsDruck™ Code:</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13561"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13562"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13578"/>
        <source>Restore Single-Sheet Backup</source>
        <translation>Stelle Einseiten Backup wiederher</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13644"/>
        <source>
               The ID field indicates that this is a SecurePrintâ¢
               Backup Type. You have either entered the ID incorrectly or
               have chosen an incorrect Backup Type.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13662"/>
        <source>Verify Wallet ID</source>
        <translation>Überprüfe Wallet ID</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13662"/>
        <source>
               There is an error in the data you entered that could not be
               fixed automatically.  Please double-check that you entered the
               text exactly as it appears on the wallet-backup page. &lt;br&gt;&lt;br&gt;
               The error occured on the &quot;%1&quot; line.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13681"/>
        <source>Verify Fragment ID</source>
        <translation>Prüfe Fragment ID</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13681"/>
        <source>
         The data you entered is for fragment:
         &lt;br&gt;&lt;br&gt; &lt;font color=&quot;%1 size=3&gt;&lt;b&gt;%2&lt;/b&gt;&lt;/font&gt;  &lt;br&gt;&lt;br&gt;
         Does this ID match the &quot;Fragment:&quot; field displayed on your backup?
         If not, click &quot;No&quot; and re-enter the fragment data.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgEnterSecurePrintCode</name>
    <message>
        <location filename="qtdialogs.py" line="13416"/>
        <source>
         This fragment file requires a SecurePrintâ¢ code.
         You will only have to enter this code once since it is the same
         on all fragments.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13421"/>
        <source>SecurePrintâ¢ Code: </source>
        <translation>SicherheitsDruck™ Code: </translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13425"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13426"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13438"/>
        <source>Enter Secure Print Code</source>
        <translation>SicherheitsDruck Code eingeben</translation>
    </message>
</context>
<context>
    <name>DlgExecLongProcess</name>
    <message>
        <location filename="qtdialogs.py" line="7724"/>
        <source>Please Wait...</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgExpWOWltData</name>
    <message>
        <location filename="qtdialogs.py" line="11284"/>
        <source>Export Watching-Only Wallet File</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11285"/>
        <source>Copy to clipboard</source>
        <translation>In Zwischenspeicher kopieren</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11287"/>
        <source>Save to Text File</source>
        <translation>Als Text Datei speichern</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11288"/>
        <source>Print Root Data</source>
        <translation>Root Daten drucken</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11300"/>
        <source>
         Watch-Only Root ID:&lt;br&gt;&lt;b&gt;%1&lt;/b&gt;
         &lt;br&gt;&lt;br&gt;
         Watch-Only Root Data:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11307"/>
        <source>Watch-Only Wallet Export</source>
        <translation>Nur-beobachten Wallet exportieren</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11319"/>
        <source>&lt;i&gt;Copied!&lt;/i&gt;</source>
        <translation>&lt;i&gt;Kopiert!&lt;/i&gt;</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11324"/>
        <source>
         &lt;center&gt;&lt;b&gt;&lt;u&gt;&lt;font size=4 color=&quot;%1&quot;&gt;Export Watch-Only
         Wallet: %2&lt;/font&gt;&lt;/u&gt;&lt;/b&gt;&lt;/center&gt;
         &lt;br&gt;
         Use a watching-only wallet on an online computer to distribute
         payment addresses, verify transactions and monitor balances, but
         without the ability to move the funds.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11332"/>
        <source>
         &lt;center&gt;&lt;b&gt;&lt;u&gt;Entire Wallet File&lt;/u&gt;&lt;/b&gt;&lt;/center&gt;
         &lt;br&gt;
         &lt;i&gt;&lt;b&gt;&lt;font color=&quot;%1&quot;&gt;(Recommended)&lt;/font&gt;&lt;/b&gt;&lt;/i&gt;
         An exact copy of your wallet file but without any of the private
         signing keys. All existing comments and labels will be carried
         with the file. Use this option if it is easy to transfer files
         from this system to the target system.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11341"/>
        <source>
         &lt;center&gt;&lt;b&gt;&lt;u&gt;Only Root Data&lt;/u&gt;&lt;/b&gt;&lt;/center&gt;
         &lt;br&gt;
         Same as above, but only five lines of text that are easy to
         print, email inline, or copy by hand.  Only produces the
         wallet addresses.   No comments or labels are carried with
         it.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11349"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
</context>
<context>
    <name>DlgExportAsciiBlock</name>
    <message>
        <location filename="MultiSigDialogs.py" line="2321"/>
        <source>Copy to Clipboard</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2322"/>
        <source>Save to File</source>
        <translation>In Datei speichern</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2323"/>
        <source>Send Email</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2324"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2346"/>
        <source>Export ASCII Block</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2378"/>
        <source>Email Triggered</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2378"/>
        <source>
            Armory attempted to execute a &quot;mailto:&quot; link which should trigger
            your email application or web browser to open a compose-email window.
            This does not work in all environments, and you might have to 
            manually copy and paste the text in the box into an email.
            </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2378"/>
        <source>Do not show this message again</source>
        <translation>Diese Nachricht nicht erneut anzeigen</translation>
    </message>
</context>
<context>
    <name>DlgExportTxHistory</name>
    <message>
        <location filename="qtdialogs.py" line="9487"/>
        <source>My Wallets</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9488"/>
        <source>Offline Wallets</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9489"/>
        <source>Other Wallets</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9492"/>
        <source>All Wallets</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9493"/>
        <source>All Lockboxes</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9494"/>
        <source>All Wallets &amp; Lockboxes</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9507"/>
        <source>Date (newest first)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9508"/>
        <source>Date (oldest first)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9513"/>
        <source>Comma-Separated Values (*.csv)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9517"/>
        <source>Use any of the following symbols:&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9528"/>
        <source>Reset to Default</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9538"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9539"/>
        <source>Export</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9548"/>
        <source>Export Format:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9555"/>
        <source>Wallet(s) to export:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9562"/>
        <source>Sort Table:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9568"/>
        <source>Date Format:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9593"/>
        <source>Example: %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9596"/>
        <source>Example: [[invalid date format]]</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9608"/>
        <source>Invalid date format</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9608"/>
        <source>Cannot create CSV without a valid format for transaction dates and times</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9663"/>
        <source>Total</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9664"/>
        <source>Spendable</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9665"/>
        <source>Unconfirmed</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9704"/>
        <source>Export Date: %1
</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9705"/>
        <source>Total Funds: %1
</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9706"/>
        <source>Spendable Funds: %1
</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9707"/>
        <source>Unconfirmed Funds: %1
</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9710"/>
        <source>Included Wallets:
</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9717"/>
        <source>%1 (lockbox),%2
</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9721"/>
        <source>Date</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9721"/>
        <source>Transaction ID</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9721"/>
        <source>Number of Confirmations</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9721"/>
        <source>Wallet ID</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9722"/>
        <source>Wallet Name</source>
        <translation>Wallet Name</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9722"/>
        <source>Credit</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9722"/>
        <source>Debit</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9722"/>
        <source>Fee (paid by this wallet)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9723"/>
        <source>Wallet Balance</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9723"/>
        <source>Total Balance</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9723"/>
        <source>Label</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgFactoryReset</name>
    <message>
        <location filename="qtdialogs.py" line="14584"/>
        <source>
         &lt;b&gt;&lt;u&gt;Armory Factory Reset&lt;/u&gt;&lt;/b&gt;
         &lt;br&gt;&lt;br&gt;
         It is &lt;i&gt;strongly&lt;/i&gt; recommended that you make backups of your
         wallets before continuing, though &lt;b&gt;wallet files will never be
         intentionally deleted!&lt;/b&gt;  All Armory
         wallet files, and the wallet.dat file used by Bitcoin Core/bitcoind
         should remain untouched in their current locations.  All Armory
         wallets will automatically be detected and loaded after the reset.
         &lt;br&gt;&lt;br&gt;
         If you are not sure which option to pick, try the &quot;lightest option&quot;
         first, and see if your problems are resolved before trying the more
         extreme options.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14602"/>
        <source>
         &lt;b&gt;Delete settings and rescan (lightest option)&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14604"/>
        <source>
         Only delete the settings file and transient network data.  The
         databases built by Armory will be rescanned (about 5-45 minutes)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14609"/>
        <source>
         &lt;b&gt;Also delete databases and rebuild&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14611"/>
        <source>
         Will delete settings, network data, and delete Armory's databases. The databases
         will be rebuilt and rescanned (45 min to 3 hours)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14616"/>
        <source>
         &lt;b&gt;Also re-download the blockchain (extreme)&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14618"/>
        <source>
         This will delete settings, network data, Armory's databases,
         &lt;b&gt;and&lt;/b&gt; Bitcoin Core's databases.  Bitcoin Core will
         have to download the blockchain again. This can take 8-72 hours depending on your 
         system's speed and connection.  Only use this if you
         suspect blockchain corruption, such as receiving StdOut/StdErr errors
         on the dashboard.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14627"/>
        <source>Do not delete settings files</source>
        <translation>Nicht die Einstellungsdateien löschen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14658"/>
        <source>Continue</source>
        <translation>Fortfahren</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14659"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14673"/>
        <source>Factory Reset</source>
        <translation>Auf Werkszustand zurücksetzen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14732"/>
        <source>Confirmation</source>
        <translation>Bestätigung</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14683"/>
        <source>
            You are about to delete your settings and force Armory to rescan
            its databases.  Are you sure you want to do this?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14698"/>
        <source>
            You are about to delete your settings and force Armory to delete
            and rebuild its databases.  Are you sure you want to do this?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14716"/>
        <source>
               You are about to delete &lt;b&gt;all&lt;/b&gt;
               blockchain databases on your system.  The Bitcoin software will
               have to redownload of blockchain data over the peer-to-peer
               network again. This can take from 8 to 72 hours depending on
               your system's speed and connection.  &lt;br&gt;&lt;br&gt;&lt;b&gt;Are you absolutely
               sure you want to do this?&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14724"/>
        <source>
               You are about to delete your settings and delete &lt;b&gt;all&lt;/b&gt;
               blockchain databases on your system.  The Bitcoin software will
               have to redownload of blockchain data over the peer-to-peer
               network again. This can take from 8 to 72 hours depending on
               your system's speed and connection.  &lt;br&gt;&lt;br&gt;&lt;b&gt;Are you absolutely
               sure you want to do this?&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14770"/>
        <source>Aborted</source>
        <translation>Abgebrochen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14770"/>
        <source>
                  You canceled the factory reset operation.  No changes were
                  made.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14786"/>
        <source>Restart Armory</source>
        <translation>Armory neu starten</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14745"/>
        <source>
               &lt;b&gt;Bitcoin Core (or bitcoind) must be closed to do the reset!&lt;/b&gt;
               Please close all Bitcoin software, &lt;u&gt;&lt;b&gt;right now&lt;/b&gt;&lt;/u&gt;,
               before clicking &quot;Continue&quot;.
               &lt;br&gt;&lt;br&gt;
               Armory will now close.  Please restart Bitcoin Core/bitcoind
               first and wait for it to finish synchronizing before restarting
               Armory.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14755"/>
        <source>
                  You canceled the factory-reset operation.  No changes were
                  made.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14764"/>
        <source>
               Armory will now close to apply the requested changes.  Please
               restart it when you are ready to start the blockchain download
               again.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14786"/>
        <source>
         Armory will now close so that the requested changes can
         be applied.</source>
        <translation>
Armory wird nun geschlossen, sodass Ihre Änderungen wirksam
werden.</translation>
    </message>
</context>
<context>
    <name>DlgForkedImports</name>
    <message>
        <location filename="qtdialogs.py" line="14797"/>
        <source>&lt;h2 style=&quot;color: red; text-align: center;&quot;&gt;Forked imported addresses have been       detected in your wallets!!!&lt;/h2&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14800"/>
        <source>The following wallets have forked imported addresses: &lt;br&gt;&lt;br&gt;&lt;b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14803"/>
        <source>When you fix a corrupted wallet, any damaged private keys will be off       the deterministic chain. It means these private keys cannot be recreated       by your paper backup. If such private keys are encountered, Armory saves       them as forked imported private keys after it fixes the relevant wallets.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14808"/>
        <source>&lt;h1 style=&quot;color: orange;&quot;&gt; - Do not accept payments to these wallets anymore&lt;br&gt;      - Do not delete or overwrite these wallets. &lt;br&gt;       - Transfer all funds to a fresh and backed up wallet&lt;h1&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14844"/>
        <source>Forked Imported Addresses</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgFragBackup</name>
    <message>
        <location filename="qtdialogs.py" line="11637"/>
        <source>
         &lt;b&gt;&lt;u&gt;Create M-of-N Fragmented Backup&lt;/u&gt; of &quot;%1&quot; (%2)&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11676"/>
        <source>Close</source>
        <translation>Schliessen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11693"/>
        <source> Use SecurePrintâ¢
         to prevent exposing keys to printer or other devices</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11700"/>
        <source>
         SecurePrintâ¢ encrypts your backup with a code displayed on
         the screen, so that no other devices or processes has access to the
         unencrypted private keys (either network devices when printing, or
         other applications if you save a fragment to disk or USB device).
         &lt;u&gt;You must keep the SecurePrintâ¢ code with the backup!&lt;/u&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11706"/>
        <source>
         &lt;b&gt;&lt;font color=&quot;%1&quot;&gt;&lt;u&gt;IMPORTANT:&lt;/u&gt;  You must keep the
         SecurePrintâ¢ encryption code with your backup!
         Your SecurePrintâ¢ code is &lt;/font&gt;
         &lt;font color=&quot;%2&quot;&gt;%3&lt;/font&gt;&lt;font color=&quot;%4&quot;&gt;.
         All fragments for a given wallet use the
         same code.&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11766"/>
        <source>&lt;u&gt;&lt;b&gt;Required Fragments&lt;/b&gt;&lt;/u&gt; </source>
        <translation>&lt;u&gt;&lt;b&gt;Benötigte Fragmente&lt;/b&gt;&lt;/u&gt;</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11767"/>
        <source>&lt;u&gt;&lt;b&gt;Total Fragments&lt;/b&gt;&lt;/u&gt; </source>
        <translation>&lt;u&gt;&lt;b&gt;Insgesamt Fragmente&lt;/b&gt;&lt;/u&gt; </translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11771"/>
        <source>Print All Fragments</source>
        <translation>Drucke alle Fragmente</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11798"/>
        <source>
         Any &lt;font color=&quot;%1&quot;&gt;&lt;b&gt;%2&lt;/b&gt;&lt;/font&gt; of these
             &lt;font color=&quot;%1&quot;&gt;&lt;b&gt;%3&lt;/b&gt;&lt;/font&gt;
         fragments are sufficient to restore your wallet, and each fragment
         has the ID, &lt;font color=&quot;%1&quot;&gt;&lt;b&gt;%4&lt;/b&gt;&lt;/font&gt;.  All fragments with the
         same fragment ID are compatible with each other!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11813"/>
        <source>&lt;b&gt;Fragment ID:&lt;br&gt;%1-%2&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11845"/>
        <source>View/Print</source>
        <translation>Vorschau/Drucken</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11846"/>
        <source>Save to File</source>
        <translation>In Datei speichern</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11880"/>
        <source>Fragments</source>
        <translation>Fragmente</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11890"/>
        <source>Secure Backup?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11890"/>
        <source>
            You have selected to use SecurePrintâ¢ for the printed
            backups, which can also be applied to fragments saved to file.
            Doing so will require you store the SecurePrintâ¢
            code with the backup, but it will prevent unencrypted key data from
            touching any disks.  &lt;br&gt;&lt;br&gt; Do you want to encrypt the fragment
            file with the same SecurePrintâ¢ code?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11916"/>
        <source>Save Fragment</source>
        <translation>Speicher Fragment</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11916"/>
        <source>Wallet Fragments (*.frag)</source>
        <translation>Wallet Fragmente (*.frag)</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11951"/>
        <source>
         The fragment was successfully saved to the following location:
         &lt;br&gt;&lt;br&gt; %1 &lt;br&gt;&lt;br&gt; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11956"/>
        <source>
            &lt;b&gt;&lt;u&gt;&lt;font color=&quot;%1&quot;&gt;Important&lt;/font&lt;/u&gt;&lt;/b&gt;:
            The fragment was encrypted with the
            SecurePrintâ¢ encryption code.  You must keep this
            code with the backup in order to use it!  The code &lt;u&gt;is&lt;/u&gt;
            case-sensitive!
            &lt;br&gt;&lt;br&gt; &lt;font color=&quot;%2&quot; size=5&gt;&lt;b&gt;%3&lt;/b&gt;&lt;/font&gt;
            &lt;br&gt;&lt;br&gt;
            The above code &lt;u&gt;&lt;b&gt;is&lt;/b&gt;&lt;/u&gt; case-sensitive!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11966"/>
        <source>Success</source>
        <translation>Erfolg</translation>
    </message>
</context>
<context>
    <name>DlgFundLockbox</name>
    <message>
        <location filename="MultiSigDialogs.py" line="1847"/>
        <source>
         To spend from a multi-sig lockbox, one party/device must create
         a proposed spending transaction, then all parties/devices must
         review and sign that transaction.  Once it has enough signatures,
         any device, can broadcast the transaction to the network.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1853"/>
        <source>
         I am creating a new proposed spending transaction and will pass
         it to each party or device that needs to sign it.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1857"/>
        <source>
         Another party or device created the transaction, I just need 
         to review and sign it.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1861"/>
        <source>Create Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1862"/>
        <source>Review and Sign</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1863"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
</context>
<context>
    <name>DlgGenericGetPassword</name>
    <message>
        <location filename="qtdialogs.py" line="387"/>
        <source>Password:</source>
        <translation>Passwort:</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="393"/>
        <source>OK</source>
        <translation>OK</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="394"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="408"/>
        <source>Enter Password</source>
        <translation>PAsswort eingeben</translation>
    </message>
</context>
<context>
    <name>DlgHelpAbout</name>
    <message>
        <location filename="qtdialogs.py" line="8678"/>
        <source>Armory Bitcoin Wallet : Version %1-beta-%2</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8681"/>
        <source>Copyright &amp;copy; 2011-2015 Armory Technologies, Inc.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8682"/>
        <source>Copyright &amp;copy; 2016 Goatpig</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8683"/>
        <source>Licensed to Armory Technologies, Inc. under the &lt;a href=&quot;http://www.gnu.org/licenses/agpl-3.0.html&quot;&gt;Affero General Public License, Version 3&lt;/a&gt; (AGPLv3)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8687"/>
        <source>Licensed to Goatpig under the &lt;a href=&quot;https://opensource.org/licenses/mit-license.php&quot;&gt;MIT License</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8705"/>
        <source>About Armory</source>
        <translation>Über Armory</translation>
    </message>
</context>
<context>
    <name>DlgImportAddress</name>
    <message>
        <location filename="qtdialogs.py" line="2845"/>
        <source>Enter:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2847"/>
        <source>One Key</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2848"/>
        <source>Multiple Keys</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2862"/>
        <source>The key can either be imported into your wallet, 
                     or have its available balance &quot;swept&quot; to another address 
                     in your wallet.  Only import private 
                     key data if you are absolutely sure that no one else 
                     has access to it.  Otherwise, sweep it to get 
                     the funds out of it.  All standard private-key formats 
                     are supported &lt;i&gt;except for private keys created by 
                     Bitcoin Core version 0.6.0 and later (compressed)&lt;/i&gt;.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2874"/>
        <source>
                       Supported formats are any hexadecimal or Base58 
                       representation of a 32-byte private key (with or 
                       without checksums), and mini-private-key format 
                       used on Casascius physical bitcoins.  Private keys 
                       that use &lt;i&gt;compressed&lt;/i&gt; public keys are not yet 
                       supported by Armory.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2889"/>
        <source>
                   Enter a list of private keys to be &quot;swept&quot; or imported. 
                   All standard private-key formats are supported.  </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2894"/>
        <source>
                  One private key per line, in any standard format. 
                  Data may be copied directly from the &quot;Export Key Lists&quot; 
                  dialog (all text on a line preceding 
                  the key data, separated by a colon, will be ignored).</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2909"/>
        <source>
         This is from a backup with SecurePrintâ¢</source>
        <translation>
Dies ist von einem Backup mit SicherheitsDruck™</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2930"/>
        <source>Sweep any funds owned by these addresses 
                                      into your wallet

                                      Select this option if someone else gave you this key</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2933"/>
        <source>Import these addresses to your wallet

                                      Only select this option if you are positive 
                                      that no one else has access to this key</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2940"/>
        <source>Sweep any funds owned by this address
                                         into your wallet

                                         Select this option if someone else gave you this key</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2948"/>
        <source>Sweep any funds owned by this address
                                            into your wallet

                                            (Not available in offline mode)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2952"/>
        <source>
               Sweep any funds owned by this address into your wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2958"/>
        <source>
         You should never add an untrusted key to your wallet.  By choosing this
         option, you are only moving the funds into your wallet, but not the key
         itself.  You should use this option for Casascius physical bitcoins.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2963"/>
        <source>
         This option will make the key part of your wallet, meaning that it
         can be used to securely receive future payments.  &lt;b&gt;Never&lt;/b&gt; select this
         option for private keys that other people may have access to.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3002"/>
        <source>Private Key Import</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3059"/>
        <source>Invalid Private Key</source>
        <translation>Ungültiger privater Schlüssel</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3052"/>
        <source>
               You entered all zeros.  This is not a valid private key!</source>
        <translation>
Sie haben nur Nullen eingetragen. Dies ist kein zulässiger Schlüssel!</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3059"/>
        <source>The private key you have entered is actually not valid for the elliptic curve used by Bitcoin (secp256k1). Almost any 64-character hex is a valid private key &lt;b&gt;except&lt;/b&gt; for those greater than: &lt;br&gt;&lt;br&gt;fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141&lt;br&gt;&lt;br&gt;Please try a different private key.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3075"/>
        <source>Entry Error</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3075"/>
        <source>
            The private key data you supplied appears to
            contain a consistency check.  This consistency
            check failed.  Please verify you entered the
            key data correctly.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3083"/>
        <source>Invalid Data</source>
        <translation>Ungültige Daten</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3083"/>
        <source>Something went terribly
            wrong!  (key data unrecognized)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3088"/>
        <source>Unsupported key type</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3088"/>
        <source>You entered a key
            for an address that uses a compressed public key, usually produced
            in Bitcoin Core/bitcoind wallets created after version 0.6.0.  Armory
            does not yet support this key type.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3095"/>
        <source>Error Processing Key</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3095"/>
        <source>
            There was an error processing the private key data.
            Please check that you entered it correctly</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3104"/>
        <source>Verify Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3104"/>
        <source>
               The key data you entered appears to correspond to
               the following Bitcoin address:

<byte value="x9"/>' %1
               

Is this the correct address?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3116"/>
        <source>Try Again</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3116"/>
        <source>
                     It is possible that the key was supplied in a
                     &quot;reversed&quot; form.  When the data you provide is
                     reversed, the following address is obtained:

<byte value="x9"/>
                     %1 

Is this the correct address?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3180"/>
        <source>The key you entered is already part of another wallet you own:&lt;br&gt;&lt;br&gt;&lt;b&gt;Address&lt;/b&gt;: </source>
        <translation>Der eingetragene Schlüssel ist bereits Teil einer Wallet die sie besitzen:&lt;br&gt;&lt;br&gt;&lt;b&gt;Adresse&lt;/b&gt;:</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3278"/>
        <source>Duplicate Addresses!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3278"/>
        <source>You are attempting to sweep %1 addresses, but %2 of them are already part of existing wallets.  That means that some or all of the bitcoins you sweep may already be owned by you. &lt;br&gt;&lt;br&gt;Would you like to continue anyway?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3326"/>
        <source>Unlock Wallet to Import</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3328"/>
        <source>Wallet is Locked</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3328"/>
        <source>
                  Cannot import private keys without unlocking wallet!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3357"/>
        <source>Nothing Imported!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3357"/>
        <source>All addresses
               chosen to be imported are already part of this wallet.
               Nothing was imported.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3362"/>
        <source>Error!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3362"/>
        <source>
               Failed:  No addresses could be imported.
               Please check the logfile (ArmoryQt.exe.log) or the console output
               for information about why it failed. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3376"/>
        <source>Success!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3370"/>
        <source>Success: %1 private keys were imported into your wallet. &lt;br&gt;&lt;br&gt;The other %2 private keys were skipped, because they were already part of your wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3376"/>
        <source>
                     Success: %1 private keys were imported into your wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3379"/>
        <source>Partial Success!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3379"/>
        <source>
                  %1 private keys were imported into your wallet, but there were
                  also %2 addresses that could not be imported (see console
                  or log file for more information).  It is safe to try this
                  operation again: all addresses previously imported will be
                  skipped.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgImportAsciiBlock</name>
    <message>
        <location filename="MultiSigDialogs.py" line="2094"/>
        <source>Load from file</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2095"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2096"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2117"/>
        <source>Load Data</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2133"/>
        <source>Error</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2133"/>
        <source>
            There was an error reading the ASCII block entered.  Please
            make sure it was entered/copied correctly, and that you have
            copied the header and footer lines that start with &quot;=====&quot;. </source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgImportLockbox</name>
    <message>
        <location filename="MultiSigDialogs.py" line="2397"/>
        <source>
         &lt;b&gt;&lt;u&gt;Import Lockbox&lt;/u&gt;&lt;/b&gt;
         &lt;br&gt;&lt;br&gt;
         Copy the lockbox text block from file or email into the box 
         below.  If you have a file with the lockbox in it, you can
         load it using the &quot;Load Lockbox&quot; button at the bottom.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2408"/>
        <source>Load from file</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2409"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2410"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2426"/>
        <source>Import Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2432"/>
        <source>Load Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2449"/>
        <source>Non-lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2449"/>
        <source>
               You are attempting to load something that is not a Lockbox.
               Please clear the display and try again.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2455"/>
        <source>Duplicate Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2455"/>
        <source>
               You just attempted to import a lockbox with ID, %1.  This
               lockbox is already in your available list of lockboxes.
               &lt;br&gt;&lt;br&gt;
               Even with the same ID, the lockbox information 
               may be different.  Would you like to overwrite the lockbox
               information already stored for %2?</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgImportPaperWallet</name>
    <message>
        <location filename="qtdialogs.py" line="4129"/>
        <source>Root Key:</source>
        <translation>Root Schlüssel:</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4131"/>
        <source>Chain Code:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4134"/>
        <source>
          Enter the characters exactly as they are printed on the
          paper-backup page.  Alternatively, you can scan the QR
          code from another application, then copy&amp;paste into the
          entry boxes below.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4139"/>
        <source>
          The data can be entered &lt;i&gt;with&lt;/i&gt; or &lt;i&gt;without&lt;/i&gt;
          spaces, and up to
          one character per line will be corrected automatically.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4154"/>
        <source>Encrypt Wallet</source>
        <translation>Wallet verschlüsseln</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4163"/>
        <source>Recover Wallet from Paper Backup</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4239"/>
        <source>Verify Wallet ID</source>
        <translation>Überprüfe Wallet ID</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4194"/>
        <source>
               There is an error on line %1 of the data you
               entered, which could not be fixed automatically.  Please
               double-check that you entered the text exactly as it appears
               on the wallet-backup page.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4212"/>
        <source>Errors Corrected!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4212"/>
        <source>
            Detected %1 error%2 on line%3 %4
            in the data you entered.  Armory attempted to fix the
            error%5 but it is not always right.  Be sure
            to verify the &quot;Wallet Unique ID&quot; closely on the next window.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4230"/>
        <source>Duplicate Wallet!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4230"/>
        <source>
               The data you entered is for a wallet with a ID: 

 <byte value="x9"/> %1
               

You already own this wallet! 
  
               Nothing to do...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4239"/>
        <source>
               The data you entered corresponds to a wallet with a wallet ID: 

 <byte value="x9"/>
               %1 

Does this ID match the &quot;Wallet Unique ID&quot;
               printed on your paper backup?  If not, click &quot;No&quot; and reenter
               key and chain-code data again.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4254"/>
        <source>Cannot Encrypt</source>
        <translation>Kann nicht verschlüsseln</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4254"/>
        <source>
               You requested your restored wallet be encrypted, but no
               valid passphrase was supplied.  Aborting wallet recovery.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4272"/>
        <source>PaperBackup - %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4281"/>
        <source>Computing New Addresses</source>
        <translation>Berechne neue Adressen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4287"/>
        <source>Recovering wallet...</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgInconsistentWltReport</name>
    <message>
        <location filename="qtdialogs.py" line="691"/>
        <source>Inconsistent Wallet!</source>
        <translation>Inkonsistente Wallet!</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="691"/>
        <source>&lt;font color=&quot;%1&quot; size=4&gt;&lt;b&gt;&lt;u&gt;Important:&lt;/u&gt;  Wallet ConsistencyIssues Detected!&lt;/b&gt;&lt;/font&gt;&lt;br&gt;&lt;br&gt;Armory now detects certain kinds of hardware errors, and oneor more of your walletswas flagged.  The consistency logs need to be analyzed by theArmory team to determine if any further action is required.&lt;br&gt;&lt;br&gt;&lt;b&gt;This warning will pop up every time you start Armory untilthe wallet is fixed&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="719"/>
        <source>
         &lt;b&gt;&lt;u&gt;&lt;font color=&quot;%1&quot; size=4&gt;Submit Wallet Analysis Logs for
         Review&lt;/font&gt;&lt;/u&gt;&lt;/b&gt;&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="724"/>
        <source>
         Armory has detected that %1 is finconsistent,
         possibly due to hardware errors out of our control.  It &lt;u&gt;strongly
         recommended&lt;/u&gt; you submit the wallet logs to the Armory developers
         for review.  Until you hear back from an Armory developer,
         it is recommended that you:
         &lt;ul&gt;
         &lt;li&gt;&lt;b&gt;Do not delete any data in your Armory home directory&lt;/b&gt;&lt;/li&gt;
         &lt;li&gt;&lt;b&gt;Do not send or receive any funds with the affected
                wallet(s)&lt;/b&gt;&lt;/li&gt;
         &lt;li&gt;&lt;b&gt;Create a backup of the wallet analysis logs&lt;/b&gt;&lt;/li&gt;
         &lt;/ul&gt;
         </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="738"/>
        <source>Save backup of log files</source>
        <translation>Sichere Backup der log Dateien</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="742"/>
        <source>Subject:</source>
        <translation>Betreff:</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="753"/>
        <source>Close</source>
        <translation>Schliessen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="777"/>
        <source>Inconsistent Wallet</source>
        <translation>Inkonsistente Wallet</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="992"/>
        <source>Not saved</source>
        <translation>Nicht gespeichert</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="992"/>
        <source>
            You canceled the backup operation.  No backup was made.</source>
        <translation>
Backup Vorgang abgebrochen. Es wurde kein Backup erstellt.</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="999"/>
        <source>Success</source>
        <translation>Erfolg</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="999"/>
        <source>The wallet logs were successfully saved to the followinglocation:&lt;br&gt;&lt;br&gt;%1&lt;br&gt;&lt;br&gt;It is still important to complete the rest of this formand submit the data to the Armory team for review!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1010"/>
        <source>Save Failed</source>
        <translation>Speichern fehlgeschlagen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1010"/>
        <source>There was an
            error saving a copy of your log files</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgInflatedQR</name>
    <message>
        <location filename="qtdefines.py" line="906"/>
        <source>&lt;b&gt;Double-click or press ESC to close&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgInstallLinux</name>
    <message>
        <location filename="qtdialogs.py" line="10622"/>
        <source>If you have manually installed Bitcoin Core or bitcoind on this system before, it is recommended you use the method here you previously used.  If you get errors using this option, try using the manual instructions below.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10627"/>
        <source>Install from bitcoin.org PPA (Ubuntu only)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10628"/>
        <source>Download and unpack binaries (All Linux)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10639"/>
        <source>&lt;b&gt;Install PPA for me (Ubuntu only):&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10640"/>
        <source>Have Armory install the PPA for you.  The does not work on all systems, so try the manual instructions below, if it fails.  Using the PPA will install the Bitcoin software using your system&apos;s package manager, and you will be notified of updates along with other software on your system.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10646"/>
        <source>Install Bitcoin PPA</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10648"/>
        <source>Click to install the Bitcoin PPA for Ubuntu</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10655"/>
        <source>&lt;b&gt;Manual PPA Installation:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10656"/>
        <source>Open a terminal window and copy the following three commands one-by-one, pressing [ENTER] after each one.  You can open a terminal by hitting Alt-F2 and typing &quot;terminal&quot; (without quotes), or in the &quot;Applications&quot; menu under &quot;Accessories&quot;.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10687"/>
        <source>&lt;b&gt;Download and set it up for me!  (All Linux):&lt;/b&gt;&lt;br&gt;&lt;br&gt;Armory will download and verify the binaries from https://bitcoin.org.  Your Armory settings will automatically be adjusted to point to that as the installation directory.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10693"/>
        <source>Install for me!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10695"/>
        <source>Select custom download location</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgIntroMessage</name>
    <message>
        <location filename="qtdialogs.py" line="4016"/>
        <source>&lt;b&gt;Welcome to Armory!&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4019"/>
        <source>&lt;i&gt;The most advanced Bitcoin Client on Earth!&lt;/i&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4022"/>
        <source>&lt;b&gt;You are about to use the most secure and feature-rich Bitcoin clientsoftware available!&lt;/b&gt;  But please remember, this softwareis still &lt;i&gt;Beta&lt;/i&gt; - Armory developers will not be held responsiblefor loss of bitcoins resulting from the use of this software!&lt;br&gt;&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4037"/>
        <source>Do not show this window again</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4045"/>
        <source>Create Your First Wallet!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4046"/>
        <source>Import Existing Wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4047"/>
        <source>Skip</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4061"/>
        <source>OK!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4076"/>
        <source>Greetings!</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgKeypoolSettings</name>
    <message>
        <location filename="qtdialogs.py" line="2516"/>
        <source>Armory pre-computes a pool of addresses beyond the last address you have used, and keeps them in your wallet to &quot;look-ahead.&quot;  One reason it does this is in case you have restored this wallet from a backup, and Armory does not know how many addresses you have actually used. &lt;br&gt;&lt;br&gt;If this wallet was restored from a backup and was very active after it was backed up, then it is possible Armory did not pre-compute enough addresses to find your entire balance.  &lt;b&gt;This condition is rare&lt;/b&gt;, but it can happen.  You may extend the keypool manually, below.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2529"/>
        <source>Addresses used: </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2530"/>
        <source>Addresses computed: </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2534"/>
        <source>Compute this many more addresses: </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2539"/>
        <source>Address computation is very slow.  It may take up to one minute to compute 200-1000 addresses (system-dependent).  Only generate as many as you think you need.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2546"/>
        <source>Compute</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2547"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2571"/>
        <source>Extend Address Pool</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2590"/>
        <source>Invalid input</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2590"/>
        <source>
            The value you entered is invalid.  Please enter a positive 
            number of addresses to generate.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2596"/>
        <source>Are you sure?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2596"/>
        <source>You have entered that you want to compute %1 more addressesfor this wallet.  This operation will take a very long time, and Armory will become unresponsive until the computation is finished.  Armory estimates it will take about %2 minutes.&lt;br&gt;&lt;br&gt;Do you want to continue?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2608"/>
        <source>&lt;font color=&quot;%1&quot;&gt;Calculating...&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2613"/>
        <source>Computing New Addresses</source>
        <translation>Berechne neue Adressen</translation>
    </message>
</context>
<context>
    <name>DlgLockboxEditor</name>
    <message>
        <location filename="MultiSigDialogs.py" line="35"/>
        <source>
         &lt;b&gt;&lt;u&gt;&lt;font size=5 color=&quot;%1&quot;&gt;Create Multi-signature Lockbox&lt;/font&gt;&lt;/u&gt;
         </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="39"/>
        <source>
         Create a &quot;lockbox&quot; to hold coins that have signing authority split 
         between multiple devices for personal funds, or split between 
         multiple parties for escrow.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="44"/>
        <source>
         &lt;b&gt;&lt;u&gt;NOTE:&lt;/u&gt; Multi-sig &quot;lockboxes&quot; require &lt;u&gt;public keys&lt;/u&gt;, not 
         the address strings most Bitcoin users are familiar with.&lt;/b&gt;
         &lt;a href=&quot;None&quot;&gt;Click for more info&lt;/a&gt;.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="50"/>
        <source>Public Key Information</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="50"/>
        <source>
            A public key is much longer than an
            address string, and always starts with &quot;02&quot;, &quot;03&quot; or &quot;04&quot;. 
            Most wallet applications do not provide an easy way to access  
            a public key associated with a given address.  This is easiest
            if everyone is using Armory. 
            &lt;br&gt;&lt;br&gt;
            The address book buttons next to each input box below will show you 
            normal address strings, but will enter the correct public 
            key of the address you select.  
            &lt;br&gt;&lt;br&gt;
            If you are creating this lockbox with other
            Armory users, they can use the &quot;Select Public Key&quot; button
            from the Lockbox Manager dashboard to pick a key and enter
            their contact info.  You can use the &quot;Import&quot; button
            on each public key line to import the data they send you.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="112"/>
        <source>
            Public Key #&lt;font size=4 color=&quot;%1&quot;&gt;%2&lt;/font&gt;:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="114"/>
        <source>Name or ID:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="125"/>
        <source>Edit</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="126"/>
        <source>Import</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="164"/>
        <source>Exit</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="165"/>
        <source>Save Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="177"/>
        <source>Set extended info</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="236"/>
        <source>&lt;font color=&quot;%1&quot; size=4&gt;&lt;b&gt;Create
         Multi-Sig Lockbox&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="240"/>
        <source>&lt;b&gt;Required Signatures (M)&lt;/b&gt; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="242"/>
        <source>&lt;b&gt;Total Signers (N)&lt;/b&gt; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="245"/>
        <source> - OF - </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="248"/>
        <source>Clear All</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="303"/>
        <source>Import Public Key Block</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="304"/>
        <source>
         &lt;center&gt;&lt;b&gt;&lt;u&gt;Import Public Key Block&lt;/u&gt;&lt;/b&gt;&lt;/center&gt;
         &lt;br&gt;
         Copy and paste a PUBLICKEY block into the text field below, 
         or load it from file.  PUBLICKEY files usually have the 
         extension &lt;i&gt;*.lockbox.pub&lt;/i&gt;.  If you were given a chunk of hex
         characters starting with &quot;02&quot;, &quot;03&quot; or &quot;04&quot;, that is a raw public 
         key and can be entered directly into the public key field in the
         lockbox creation window.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgLockboxManager</name>
    <message>
        <location filename="MultiSigDialogs.py" line="652"/>
        <source>&lt;br&gt;Double-click on a lockbox to edit</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="654"/>
        <source>
         &lt;font color=&quot;%1&quot; size=4&gt;&lt;b&gt;Manage Multi-Sig Lockboxes&lt;/b&gt;&lt;/font&gt;
         %2</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="699"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="765"/>
        <source>Dashboard</source>
        <translation>Übersichtsseite</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="766"/>
        <source>Info</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="767"/>
        <source>Transactions</source>
        <translation>Transaktion</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="820"/>
        <source>Create Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="823"/>
        <source>Collect public keys</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="824"/>
        <source>Create a lockbox by collecting public keys
                                from each device or person that will be 
                                a signing authority over the funds.  Once
                                created you will be given a chunk of text
                                to send to each party so they can recognize
                                and sign transactions related to the 
                                lockbox.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="835"/>
        <source>Select Public Key</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="838"/>
        <source>Send to organizer</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="839"/>
        <source>In order to create a lockbox all devices 
                                and/or parties need to provide a public key 
                                that they control to be merged by the 
                                organizer.  Once all keys are collected,
                                the organizer will send you the final
                                lockbox definition to import.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="849"/>
        <source>Export Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="852"/>
        <source>Send to other devices or parties</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="853"/>
        <source>Export a lockbox definition to be imported
                                by other devices or parties.  Normally the 
                                lockbox organizer will do this after all public
                                keys are collected, but any participant who 
                                already has it can send it, such as if one 
                                party/device accidentally deletes it.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="859"/>
        <source>Select lockbox to export</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="863"/>
        <source>Import Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="866"/>
        <source>From organizer or other device</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="867"/>
        <source>Import a lockbox definition to begin
                                tracking its funds and to be able to
                                sign related transactions.
                                Normally, the organizer will send you 
                                the data to import after you
                                provide a public key from one of your
                                wallets.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="878"/>
        <source>Edit Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="882"/>
        <source>Edit an existing lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="883"/>
        <source>Select lockbox to edit</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="910"/>
        <source>Merge Promissory Notes</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="914"/>
        <source>Collect promissory notes from all funders
                                of a simulfunding transaction.  Use this to
                                merge them into a single transaction that 
                                the funders can review and sign.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="922"/>
        <source>Create Promissory Note</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="925"/>
        <source>Make a funding commitment to a lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="926"/>
        <source>A &quot;promissory note&quot; provides blockchain
                                information about how your wallet will 
                                contribute funds to a simulfunding transaction.
                                A promissory note does &lt;b&gt;not&lt;/b&gt;
                                move any money in your wallet.  The organizer
                                will create a single transaction that includes
                                all promissory notes and you will be able to 
                                review it in its entirety before signing.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="934"/>
        <source>Select lockbox to commit funds to</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="935"/>
        <source>Must be online to create</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="938"/>
        <source>Review and Sign</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="941"/>
        <source>Multi-sig spend or simulfunding</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="942"/>
        <source>Review and sign any lockbox-related
                                transaction that requires multiple 
                                signatures.  This includes spending 
                                transactions from a regular lockbox,
                                as well as completing a simulfunding
                                transaction.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="952"/>
        <source>Create Spending Tx</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="955"/>
        <source>Send bitcoins from lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="956"/>
        <source>Create a proposed transaction sending bitcoins
                                to an address, wallet or another lockbox.  
                                The transaction will not be final until enough
                                signatures have been collected and then 
                                broadcast from an online computer.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="961"/>
        <source>Select lockbox to spend from</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="962"/>
        <source>Must be online to spend</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="966"/>
        <source>Collect Sigs &amp;&amp; Broadcast</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="969"/>
        <source>Merge signatures to finalize</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="970"/>
        <source>Merge signatures and broadcast transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="972"/>
        <source>(must be online to broadcast)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="982"/>
        <source>SimulFund</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="986"/>
        <source>
         If this lockbox will be funded by multiple parties and not all
         parties are fully trusted, use &quot;simulfunding&quot; to ensure that funds 
         are committed at the same time.  Check the &quot;Simul&quot; box to show 
         simulfunding options in the table.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1133"/>
        <source>Fund from Wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1134"/>
        <source>QR Code</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1135"/>
        <source>Request Payment</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1172"/>
        <source>Copy Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1187"/>
        <source>
               Anyone can send funds to this lockbox using this
               Bitcoin address: &lt;br&gt;&lt;b&gt;%1&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1322"/>
        <source>Invalid Tx</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1322"/>
        <source>The transaction you requested be displayed does not exist in Armory&apos;s database.  This is unusual...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1353"/>
        <source>View on blockexplorer.com</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1356"/>
        <source>View on blockchain.info</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1360"/>
        <source>View Details</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1362"/>
        <source>Change Comment</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1363"/>
        <source>Copy Transaction ID</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1435"/>
        <source>Could not open browser</source>
        <translation>Konnte Browser nicht öffnen</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1374"/>
        <source>Armory encountered an error opening your web browser.  To view this transaction on blockchain.info, please copy and paste the following URL into your browser: &lt;br&gt;&lt;br&gt;%1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1396"/>
        <source>Copy P2SH address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1397"/>
        <source>Display address QR code</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1399"/>
        <source>View address on %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1402"/>
        <source>Request payment to this lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1403"/>
        <source>Copy hash160 value (hex)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1404"/>
        <source>Copy balance</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1405"/>
        <source>Delete Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1407"/>
        <source>Rescan Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1435"/>
        <source>
                  Armory encountered an error opening your web browser.  To view 
                  this address on %1, please copy and paste
                  the following URL into your browser: 
                  &lt;br&gt;&lt;br&gt;
                  &lt;a href=&quot;%2&quot;&gt;%3&lt;/a&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1450"/>
        <source>Compatibility Warning</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1450"/>
        <source>You are about to request payment to a &quot;P2SH&quot; address
                  which is the format used for receiving to multi-signature
                  addresses/lockboxes.  &quot;P2SH&quot; are like regular Bitcoin 
                  addresses but start with %1 instead of %2.
                  &lt;br&gt;&lt;br&gt;
                  Unfortunately, not all software and services support sending 
                  to P2SH addresses.  If the sender or service indicates   
                  an error sending to this address, you might have to request
                  payment to a regular wallet address and then send the funds
                  from that wallet to the lockbox once it is confirmed.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1450"/>
        <source>Do not show this message again</source>
        <translation>Diese Nachricht nicht erneut anzeigen</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1713"/>
        <source>Confirm Delete</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1474"/>
        <source>
               &quot;Removing&quot; a lockbox does not delete any signing keys, so you 
               maintain signing authority for any coins that are sent there.     
               However, it will remove it from the list of lockboxes, and you
               will have to re-import it later in order to send any funds
               to or from the lockbox.
               &lt;br&gt;&lt;br&gt;
               You are about to remove the following lockbox:
               &lt;br&gt;&lt;br&gt;
               &lt;font color=&quot;%1&quot;&gt;%2&lt;/font&gt; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1494"/>
        <source>Confirm Rescan</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1494"/>
        <source>
               Rescaning a Lockbox will make it unavailable for the duration
               of the process
               &lt;br&gt;&lt;br&gt;
               You are about to rescan the following lockbox:
               &lt;br&gt;&lt;br&gt;
               &lt;font color=&quot;%1&quot;&gt;%2&lt;/font&gt; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1534"/>
        <source> &lt;br&gt;&lt;br&gt;&lt;font color=&quot;%1&quot;&gt;&lt;center&gt;&lt;b&gt;
            Select a lockbox from the table above to view its info&lt;/b&gt;&lt;/center&gt;
            &lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1589"/>
        <source>&lt;font color=&quot;%1&quot; size=4&gt;&lt;center&gt;&lt;u&gt;Lockbox Information for
         &lt;b&gt;%2&lt;/b&gt;&lt;/u&gt;&lt;/center&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1591"/>
        <source>&lt;b&gt;Multisig:&lt;/b&gt;&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;%1-of-%2</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1592"/>
        <source>&lt;b&gt;Lockbox ID:&lt;/b&gt;&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;%1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1593"/>
        <source>&lt;b&gt;P2SH Address:&lt;/b&gt;&amp;nbsp;&amp;nbsp;%1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1594"/>
        <source>&lt;b&gt;Lockbox Name:&lt;/b&gt;&amp;nbsp;&amp;nbsp;%1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1595"/>
        <source>&lt;b&gt;Created:&lt;/b&gt;&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;%1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1596"/>
        <source>&lt;b&gt;Extended Info:&lt;/b&gt;&lt;hr&gt;&lt;blockquote&gt;%1&lt;/blockquote&gt;&lt;hr&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1597"/>
        <source>&lt;b&gt;Stored Key Details&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1607"/>
        <source>&amp;nbsp;&amp;nbsp;&lt;b&gt;Key #%1&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1608"/>
        <source>&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;&lt;b&gt;Name/ID:&lt;/b&gt;&amp;nbsp;%1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1609"/>
        <source>&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;&lt;b&gt;Address:&lt;/b&gt;&amp;nbsp;%1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1610"/>
        <source>&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;&amp;nbsp;&lt;b&gt;PubKey:&lt;/b&gt;&amp;nbsp;&amp;nbsp;%1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1612"/>
        <source>&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1781"/>
        <source>Import Signature Collector</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1692"/>
        <source>
         Import a &lt;i&gt;Signature Collector&lt;/i&gt; block to review and
         sign the lockbox-spend or simulfunding transaction.  This text block 
         is produced by the organizer and will contain
         &quot;=====TXSIGCOLLECT&quot; on the first line.   Or you can import it from
         a file, which is saved by default with a
         &lt;i&gt;*.sigcollect.tx&lt;/i&gt; extension.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1713"/>
        <source>
         &quot;Removing&quot; a lockbox does not delete any signing keys, so you 
         maintain signing authority for any coins that are sent there.     
         However, Armory will stop tracking its history and balance, and you
         will have to re-import it later in order to sign any transactions.
         &lt;br&gt;&lt;br&gt;
         You are about to remove the following lockbox:
         &lt;br&gt;&lt;br&gt;
         &lt;font color=&quot;%1&quot;&gt;%2&lt;/font&gt; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1736"/>
        <source>[WARNING]</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1736"/>
        <source>
         &lt;b&gt;&lt;font color=&quot;%1&quot;&gt;WARNING:&lt;/font&gt; &lt;/b&gt;
         If this lockbox is being used to hold escrow for multiple parties, and
         requires being funded by multiple participants, you &lt;u&gt;must&lt;/u&gt; use
         a special funding process to ensure simultaneous funding.  Otherwise,
         one of the other parties may be able to scam you!  
         &lt;br&gt;&lt;br&gt;
         It is safe to continue if any of the following conditions are true:
         &lt;ul&gt;
            &lt;li&gt;You are the only one expected to fund this lockbox/escrow&lt;/li&gt;
            &lt;li&gt;All other parties in the lockbox/escrow are fully trusted&lt;/li&gt;
            &lt;li&gt;This lockbox is being used for personal savings&lt;/li&gt;
         &lt;/ul&gt;
         If the above does not apply to you, please press &quot;Cancel&quot; and 
         select the &quot;Simul&quot; checkbox on the lockbox dashboard.
         </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1761"/>
        <source>Funding %1-of-%2</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1782"/>
        <source>
            Import a &lt;i&gt;Signature Collector&lt;/i&gt; text block to review and
            sign the simulfunding transaction.  This text block is produced
            by the party that collected and merged all the promissory notes.
            Files containing signature-collecting data usually end with
            &lt;i&gt;*.sigcollect.tx&lt;/i&gt;.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgMergePromNotes</name>
    <message>
        <location filename="MultiSigDialogs.py" line="3505"/>
        <source>
         &lt;font color=&quot;%1&quot; size=4&gt;&lt;b&gt;Merge Promissory Notes
         &lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3510"/>
        <source>
         Collect promissory notes from two or more parties
         to combine them into a single &lt;i&gt;simulfunding&lt;/i&gt; transaction.  Once
         all notes are collected you will be able to
         send it to each contributing party for review and signing.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3523"/>
        <source>Lockbox Being Funded</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3526"/>
        <source>Address Being Funded</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3563"/>
        <source>Loaded Promissory Notes</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3564"/>
        <source>
         &lt;font size=4&gt;&lt;b&gt;No Promissory Notes Have Been Added&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3638"/>
        <source>Import Promissory Note</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3587"/>
        <source>Create &amp;&amp; Add Promissory Note</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3592"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3593"/>
        <source>Use bare multisig (no P2SH)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3594"/>
        <source>
         EXPERT OPTION:  Do not check this box unless you know what it means
                         and you need it!  Forces Armory to exposes public 
                         keys to the blockchain before the funds are spent.  
                         This is only needed for very specific use cases, 
                         and otherwise creates blockchain bloat.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3600"/>
        <source>Continue</source>
        <translation>Fortfahren</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3639"/>
        <source>
         Import a promissory note to add to this simulfunding transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3651"/>
        <source>Invalid Promissory Note</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3651"/>
        <source>
            No promissory note was loaded.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3662"/>
        <source>Not Online</source>
        <translation>Nicht online</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3662"/>
        <source>
            Armory is currently in offline mode and cannot create any 
            transactions or promissory notes.  You can only merge 
            pre-existing promissory notes at this time.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3690"/>
        <source>Already Loaded</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3690"/>
        <source> This 
            promissory note has already been loaded!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3729"/>
        <source>Mismatched Funding Target</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3729"/>
        <source>
            The promissory note you loaded is for a different funding target. 
            Please make sure that all promissory notes are for the target
            specified on the previous window</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3771"/>
        <source>Nothing Loaded</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3771"/>
        <source>
            No promissory notes were loaded.  Cannot create simulfunding 
            transaction.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3777"/>
        <source>Merging One Note</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3777"/>
        <source>
            Only one promissory note was entered, so there
            is nothing to merge.  
            &lt;br&gt;&lt;br&gt;
            The simulfunding interface is intended to merge promissory notes
            from multiple parties to ensure simultaneous funding 
            for escrow.  If only person is funding, they 
            can simply send money to the address or lockbox like they would 
            any other transaction, without going through the simulfunding 
            interface.
            &lt;br&gt;&lt;br&gt;
            Click &quot;Ok&quot; to continue to the multi-signing interface, but there
            will only be one input to sign.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3823"/>
        <source>Export Simulfunding Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3824"/>
        <source>
         The text block below contains the simulfunding transaction to be
         signed by all parties funding this lockbox.  Copy the text block
         into an email to all parties contributing funds.  Each party can
         review the final simulfunding transaction, add their signature(s),
         then send back to you to finalize it.  
         &lt;br&gt;&lt;br&gt;
         When you click &quot;Done&quot;, you will be taken to a window that you can
         use to merge the TXSIGCOLLECT blocks from all parties and broadcast
         the final transaction.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgMultiSpendReview</name>
    <message>
        <location filename="MultiSigDialogs.py" line="2483"/>
        <source>
         The following transaction is a proposed spend of funds controlled
         by multiple parties.  The keyholes next to each input represent 
         required signatures for the tx to be valid.  White
         means it has not yet been signed, and cannot be signed by you.  Green
         represents signatures that can be added by one of your wallets.
         Gray keyholes are already signed.
         &lt;br&gt;&lt;br&gt;
         Change outputs have been hidden where it is obvious (such as coins
         returning to the same lockbox from where it came).  If there is 
         any ambiguity, Armory will display all outputs.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgNewAddressDisp</name>
    <message>
        <location filename="qtdialogs.py" line="2667"/>
        <source>The following address can be used to receive bitcoins:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2672"/>
        <source>Copy to Clipboard</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2674"/>
        <source> or </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2685"/>
        <source>Create Clickable Link</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2689"/>
        <source>
            You can securely use this address as many times as you want. 
            However, all people to whom you give this address will 
            be able to see the number and amount of bitcoins &lt;b&gt;ever&lt;/b&gt; 
            sent to it.  Therefore, using a new address for each transaction 
            improves overall privacy, but there is no security issues 
            with reusing any address.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2726"/>
        <source>(Optional) Add a label to this address, which will be shown with any relevant transactions in the &quot;Transactions&quot; tab.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2743"/>
        <source>Bitcoins sent to this address will 
            appear in the wallet:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2757"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2769"/>
        <source>&lt;b&gt;Scan QR code with phone or other barcode reader&lt;/b&gt;&lt;br&gt;&lt;br&gt;&lt;font size=2&gt;(Double-click to expand)&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2806"/>
        <source>New Receiving Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2834"/>
        <source>&lt;i&gt;Copied!&lt;/i&gt;</source>
        <translation>&lt;i&gt;Kopiert!&lt;/i&gt;</translation>
    </message>
</context>
<context>
    <name>DlgNewWallet</name>
    <message>
        <location filename="qtdialogs.py" line="1026"/>
        <source>
         Create a new wallet for managing your funds.&lt;br&gt;
         The name and description can be changed at any time.</source>
        <translation>
Erstellen einer neuen Wallet zu Verwaltung ihres Kapitals.&lt;br&gt;
Der Name und die Beschreibung kann jeder Zeit geändert werden.</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1050"/>
        <source>
                  Armory will test your system's speed to determine the most 
                  challenging encryption settings that can be performed '
                  in a given amount of time.  High settings make it much harder 
                  for someone to guess your passphrase.  This is used for all 
                  encrypted wallets, but the default parameters can be changed below.
</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1058"/>
        <source>
                  This is the amount of time it will take for your computer 
                  to unlock your wallet after you enter your passphrase. 
                  (the actual time used will be less than the specified 
                  time, but more than one half of it).  </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1070"/>
        <source>
                  This is the &lt;b&gt;maximum&lt;/b&gt; memory that will be 
                  used as part of the encryption process.  The actual value used 
                  may be lower, depending on your system's speed.  If a 
                  low value is chosen, Armory will compensate by chaining 
                  together more calculations to meet the target time.  High 
                  memory target will make GPU-acceleration useless for 
                  guessing your passphrase.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1085"/>
        <source>Max &amp;memory usage (kB, MB):</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1108"/>
        <source>Use wallet &amp;encryption</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1110"/>
        <source>
                  Encryption prevents anyone who accesses your computer 
                  or wallet file from being able to spend your money, as  
                  long as they do not have the passphrase.
                  You can choose to encrypt your wallet at a later time 
                  through the wallet properties dialog by double clicking 
                  the wallet on the dashboard.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1119"/>
        <source>Print a paper-backup of this wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1121"/>
        <source>
                  A paper-backup allows you to recover your wallet/funds even 
                  if you lose your original wallet file, any time in the future. 
                  Because Armory uses &quot;deterministic wallets,&quot; 
                  a single backup when the wallet is first made is sufficient 
                  for all future transactions (except ones to imported 
                  addresses).


                  Anyone who gets ahold of your paper backup will be able to spend 
                  the money in your wallet, so please secure it appropriately.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1132"/>
        <source>Accept</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1133"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1134"/>
        <source>Adv. Encrypt Options&gt;&gt;&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1149"/>
        <source>Import wallet...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1177"/>
        <source>Create Armory wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1192"/>
        <source>Invalid wallet name</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1192"/>
        <source>You must enter a name for this wallet, up to 32 characters.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1198"/>
        <source>Input too long</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1198"/>
        <source>
                  The wallet description is limited to 256 characters.  Only the first 
                  256 characters will be used.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1228"/>
        <source>Invalid KDF Parameters</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1216"/>
        <source>
               Please specify a compute time no more than 20 seconds.  
               Values above one second are usually unnecessary.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1228"/>
        <source>Please specify a maximum memory usage between 32 kB 
               and 2048 MB.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1235"/>
        <source>Invalid Input</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1235"/>
        <source>
            Please specify time with units, such as 
            &quot;250 ms&quot; or &quot;2.1 s&quot;.  Specify memory as kB or MB, such as 
            &quot;32 MB&quot; or &quot;256 kB&quot;. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1246"/>
        <source>Import Wallet File</source>
        <translation>Lese Wallet Datei ein</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1246"/>
        <source>Wallet files (*.wallet);; All files (*)</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgOfflineSelect</name>
    <message>
        <location filename="qtdialogs.py" line="5112"/>
        <source>In order to execute an offline transaction, three steps mustbe followed: &lt;br&gt;&lt;br&gt;<byte value="x9"/>(1) &lt;u&gt;On&lt;/u&gt;line Computer:  Create the unsigned transaction&lt;br&gt;<byte value="x9"/>(2) &lt;u&gt;Off&lt;/u&gt;line Computer: Get the transaction signed&lt;br&gt;<byte value="x9"/>(3) &lt;u&gt;On&lt;/u&gt;line Computer:  Broadcast the signed transaction&lt;br&gt;&lt;br&gt;You must create the transaction using a watch-only wallet on an onlinesystem, but watch-only wallets cannot sign it.  Only the offline systemcan create a valid signature.  The easiest way to execute all three stepsis to use a USB key to move the data between computers.&lt;br&gt;&lt;br&gt;All the data saved to the removable medium during all three steps arecompletely safe and do not reveal any private information that would benefit anattacker trying to steal your funds.  However, this transaction data doesreveal some addresses in your wallet, and may represent a breach of&lt;i&gt;privacy&lt;/i&gt; if not protected.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5128"/>
        <source>Create New Offline Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5129"/>
        <source>Sign and/or Broadcast Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5133"/>
        <source>No wallets available!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5136"/>
        <source>Sign Offline Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5139"/>
        <source>No watching-only wallets available!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5144"/>
        <source>&lt;&lt;&lt; Go Back</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5155"/>
        <source>
         Create a transaction from an Offline/Watching-Only wallet
         to be signed by the computer with the full wallet </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5159"/>
        <source>
         Review an unsigned transaction and sign it if you have
         the private keys needed for it </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5163"/>
        <source>
         Send a pre-signed transaction to the Bitcoin network to finalize it</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgOfflineTxCreated</name>
    <message>
        <location filename="qtdialogs.py" line="5093"/>
        <source>Review Offline Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5069"/>
        <source>Continue</source>
        <translation>Fortfahren</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5071"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5074"/>
        <source>
         By clicking Done you will exit end the offline transaction process for now.
         When you are ready to sign and/or broadcast the transaction, click the Offline
         Transactions button in the main window, then click the Sign and/or
         Broadcast Transaction button in the Select Offline Action dialog.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5080"/>
        <source>
         By clicking Continue you will continue to the next step in the offline
         transaction process to sign and/or broadcast the transaction.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgPasswd3</name>
    <message>
        <location filename="qtdialogs.py" line="1387"/>
        <source>&lt;font color=&quot;red&quot;&gt;&lt;b&gt;!!! DO NOT FORGET YOUR PASSPHRASE !!!&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1390"/>
        <source>&lt;b&gt;No one can help you recover you bitcoins if you forget the passphrase and don&apos;t have a paper backup!&lt;/b&gt; Your wallet and any &lt;u&gt;digital&lt;/u&gt; backups are useless if you forget it.  &lt;br&gt;&lt;br&gt;A &lt;u&gt;paper&lt;/u&gt; backup protects your wallet forever, against hard-drive loss and losing your passphrase.  It also protects you from theft, if the wallet was encrypted and the paper backup was not stolen with it.  Please make a paper backup and keep it in a safe place.&lt;br&gt;&lt;br&gt;Please enter your passphrase a third time to indicate that you are aware of the risks of losing your passphrase!&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1410"/>
        <source>Accept</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1411"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1423"/>
        <source>WARNING!</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgPrintBackup</name>
    <message>
        <location filename="qtdialogs.py" line="6835"/>
        <source>Error Creating Backup</source>
        <translation>Fehler beim Erstellen des Backups</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6835"/>
        <source>
            There was an error with the backup creator.  The operation is being
            canceled to avoid making bad backups!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6916"/>
        <source>Print imported keys</source>
        <translation>Drucke eingelesene Schlüssel</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6919"/>
        <source>Page:</source>
        <translation>Seite:</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6932"/>
        <source>
         Use SecurePrintâ¢ to prevent exposing keys to printer or other
         network devices</source>
        <translation>
Nutze SicherheitsDruck™ um entblößen der Schlüssel zum Drucker
oder anderen Netzwerkgeräten zu vermeiden </translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6939"/>
        <source>
         SecurePrintâ¢ encrypts your backup with a code displayed on
         the screen, so that no other devices on your network see the sensitive
         data when you send it to the printer.  If you turn on
         SecurePrintâ¢ &lt;u&gt;you must write the code on the page after
         it is done printing!&lt;/u&gt;  There is no point in using this feature if
         you copy the data by hand.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6947"/>
        <source>
         &lt;b&gt;&lt;font color=&quot;%1&quot;&gt;&lt;u&gt;IMPORTANT:&lt;/u&gt;  You must write the SecurePrintâ¢
         encryption code on each printed backup page!  Your SecurePrintâ¢ code is &lt;/font&gt;
         &lt;font color=&quot;%2&quot;&gt;%3&lt;/font&gt;.  &lt;font color=&quot;%4&quot;&gt;Your backup will not work
         if this code is lost!&lt;/font&gt; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6965"/>
        <source>
            &lt;b&gt;&lt;u&gt;Print Wallet Backup Fragments&lt;/u&gt;&lt;/b&gt;&lt;br&gt;&lt;br&gt;
            When any %1 of these fragments are combined, all &lt;u&gt;previous
            &lt;b&gt;and&lt;/b&gt; future&lt;/u&gt; addresses generated by this wallet will be
            restored, giving you complete access to your bitcoins.  The
            data can be copied by hand if a working printer is not
            available.  Please make sure that all data lines contain
            &lt;b&gt;9 columns&lt;/b&gt;
            of &lt;b&gt;4 characters each&lt;/b&gt; (excluding &quot;ID&quot; lines).</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="6976"/>
        <source>
            &lt;b&gt;&lt;u&gt;Print a Forever-Backup&lt;/u&gt;&lt;/b&gt;&lt;br&gt;&lt;br&gt;
            Printing this sheet protects all &lt;u&gt;previous &lt;b&gt;and&lt;/b&gt; future&lt;/u&gt; addresses
            generated by this wallet!  You can copy the &quot;Root Key&quot; %1
            by hand if a working printer is not available.  Please make sure that
            all data lines contain &lt;b&gt;9 columns&lt;/b&gt;
            of &lt;b&gt;4 characters each&lt;/b&gt;.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7059"/>
        <source>Lots to Print!</source>
        <translation>Viel zu drucken!</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7059"/>
        <source>
            This wallet contains &lt;b&gt;%1&lt;/b&gt; imported keys, which will require
            &lt;b&gt;%2&lt;/b&gt; pages to print.  Not only will this use a lot of paper,
            it will be a lot of work to manually type in these keys in the
            event that you need to restore this backup. It is recommended
            that you do &lt;u&gt;not&lt;/u&gt; print your imported keys and instead make
            a digital backup, which can be restored instantly if needed.
            &lt;br&gt;&lt;br&gt; Do you want to print the imported keys, anyway?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7093"/>
        <source>of %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7136"/>
        <source>SecurePrint Code</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7136"/>
        <source>
               &lt;br&gt;&lt;b&gt;You must write your SecurePrintâ¢
               code on each sheet of paper you just printed!&lt;/b&gt;
               Write it in the red box in upper-right corner
               of each printed page. &lt;br&gt;&lt;br&gt;SecurePrintâ¢ code:
               &lt;font color=&quot;%1&quot; size=5&gt;&lt;b&gt;%2&lt;/b&gt;&lt;/font&gt; &lt;br&gt;&lt;br&gt;
               &lt;b&gt;NOTE: the above code &lt;u&gt;is&lt;/u&gt; case-sensitive!&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7204"/>
        <source>Single-Sheet </source>
        <translation>Einseitig</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7206"/>
        <source>Imported Keys </source>
        <translation>Keys Einlesen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7208"/>
        <source>Fragmented Backup (%1-of-%2)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7225"/>
        <source>&lt;b&gt;%1-&lt;font color=&quot;%2&quot;&gt;#%2&lt;/font&gt;&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7237"/>
        <source>
            Any subset of &lt;font color=&quot;%1&quot;&gt;&lt;b&gt;%2&lt;/b&gt;&lt;/font&gt; fragments with this
            ID (&lt;font color=&quot;%3&quot;&gt;&lt;b&gt;%4&lt;/b&gt;&lt;/font&gt;) are sufficient to recover all the
            coins contained in this wallet.  To optimize the physical security of
            your wallet, please store the fragments in different locations.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7245"/>
        <source>
            &lt;font color=&quot;#aa0000&quot;&gt;&lt;b&gt;WARNING:&lt;/b&gt;&lt;/font&gt; Anyone who has access to this
            page has access to all the bitcoins in %1!  Please keep this
            page in a safe place.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7263"/>
        <source>
            The following %1 lines backup all addresses
            &lt;i&gt;ever generated&lt;/i&gt; by this wallet (previous and future).
            This can be used to recover your wallet if you forget your passphrase or
            suffer hardware failure and lose your wallet files. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7270"/>
        <source>
               The following is a list of all private keys imported into your
               wallet before this backup was made.   These keys are encrypted
               with the SecurePrintâ¢ code and can only be restored
               by entering them into Armory.  Print a copy of this backup without
               the SecurePrintâ¢ option if you want to be able to import
               them into another application.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7278"/>
        <source>
               The following is a list of all private keys imported into your
               wallet before this backup was made.  Each one must be copied
               manually into the application where you wish to import them.  </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7284"/>
        <source>
            The following is fragment &lt;font color=&quot;%1&quot;&gt;&lt;b&gt;#%2&lt;/b&gt;&lt;/font&gt; for this
            wallet. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7307"/>
        <source>
            &lt;b&gt;&lt;font color=&quot;#770000&quot;&gt;CRITICAL:&lt;/font&gt;  This backup will not
            work without the SecurePrintâ¢
            code displayed on the screen during printing.
            Copy it here in ink:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7414"/>
        <source>
         The following QR code is for convenience only.  It contains the
         exact same data as the %1 lines above.  If you copy this backup
         by hand, you can safely ignore this QR code. </source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgProgress</name>
    <message>
        <location filename="qtdialogs.py" line="14124"/>
        <source>Close</source>
        <translation>Schliessen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14155"/>
        <source>Enter Passphrase</source>
        <translation>Passwort eingeben</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14245"/>
        <source>%1: %2%%</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14274"/>
        <source>Progress Bar</source>
        <translation>Fortschrittsanzeige</translation>
    </message>
</context>
<context>
    <name>DlgRegAndTest</name>
    <message>
        <location filename="qtdialogs.py" line="15111"/>
        <source>Error: You cannot run the Regression Test network and Bitcoin Test Network at the same time.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgRemoveAddress</name>
    <message>
        <location filename="qtdialogs.py" line="4629"/>
        <source>&lt;b&gt;!!! WARNING !!!&lt;/b&gt;

</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4633"/>
        <source>&lt;i&gt;You have requested that the following address
                            be deleted from your wallet:&lt;/i&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4641"/>
        <source>Address:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4644"/>
        <source>Comment:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4648"/>
        <source>In Wallet:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4655"/>
        <source>Address Balance (w/ unconfirmed):</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4692"/>
        <source>Do you want to delete this address?  No other addresses in this
            wallet will be affected.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4706"/>
        <source>Confirm Delete Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4710"/>
        <source>One more time...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4710"/>
        <source>
           Simply deleting an address does not prevent anyone
           from sending money to it.  If you have given this address
           to anyone in the past, make sure that they know not to
           use it again, since any bitcoins sent to it will be
           inaccessible.


           If you are maintaining an external copy of this address
           please ignore this warning


           Are you absolutely sure you want to delete %1 ?</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgRemoveWallet</name>
    <message>
        <location filename="qtdialogs.py" line="4345"/>
        <source>&lt;b&gt;!!! WARNING !!!&lt;/b&gt;

</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4349"/>
        <source>&lt;i&gt;You have requested that the following wallet
                            be removed from Armory:&lt;/i&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4357"/>
        <source>Wallet Unique ID:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4360"/>
        <source>Wallet Name:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4363"/>
        <source>Description:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4378"/>
        <source>Current Balance (w/ unconfirmed):</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4417"/>
        <source>&lt;b&gt;WALLET IS NOT EMPTY.  Only delete this wallet if you
                             have a backup on paper or saved to a another location
                             outside your settings directory.&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4422"/>
        <source>&lt;b&gt;WALLET IS PART OF A LOCKBOX.  Only delete this wallet if you
                             have a backup on paper or saved to a another location
                             outside your settings directory.&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4430"/>
        <source>Permanently delete this wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4431"/>
        <source>Delete private keys only, make watching-only</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4440"/>
        <source>
         This will delete the wallet file, removing
         all its private keys from your settings directory.
         If you intend to keep using addresses from this
         wallet, do not select this option unless the wallet
         is backed up elsewhere.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4446"/>
        <source>
         This will delete the private keys from your wallet,
         leaving you with a watching-only wallet, which can be
         used to generate addresses and monitor incoming
         payments.  This option would be used if you created
         the wallet on this computer &lt;i&gt;in order to transfer
         it to a different computer or device and want to
         remove the private data from this system for security.&lt;/i&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4456"/>
        <source>
         Print a paper backup of this wallet before deleting</source>
        <translation>
Drucke ein Papier Backup vor dem Löschen der Wallet</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4460"/>
        <source>
            This will delete the wallet file from your system.
            Since this is a watching-only wallet, no private keys
            will be deleted.</source>
        <translation>
Dies wird die Wallet Datei vom System entfernen.
Da es sich um eine nur-zuschauen Wallet handelt werden
keine privaten Schlüssel gelöscht.</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4464"/>
        <source>
            This wallet is already a watching-only wallet so this option
            is pointless</source>
        <translation>
Diese Wallet ist bereits eine nur-zuschauen Wallet, diese Funktion
ist also nutzlos</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4503"/>
        <source>
         If this box is checked, you will have the ability to print off an
         unencrypted version of your wallet before it is deleted.  &lt;b&gt;If
         printing is unsuccessful, please press *CANCEL* on the print dialog
         to prevent the delete operation from continuing&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4521"/>
        <source>Delete</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4522"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4531"/>
        <source>Delete Wallet Options</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4539"/>
        <source>Unlock Paper Backup</source>
        <translation>Papier Backup entsperren</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4541"/>
        <source>Operation Aborted</source>
        <translation>Vorgang abgebrochen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4541"/>
        <source>
              You requested a paper backup before deleting the wallet, but
              clicked &quot;Cancel&quot; on the backup printing window.  So, the delete
              operation was canceled as well.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4554"/>
        <source>Confirm Delete</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4554"/>
        <source>You are about to delete a watching-only wallet.  Are you sure
         you want to do this?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4564"/>
        <source>Are you absolutely sure?!?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4558"/>
        <source>Are you absolutely sure you want to permanently delete
         this wallet?  Unless this wallet is saved on another device
         you will permanently lose access to all the addresses in this
         wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4564"/>
        <source>&lt;i&gt;This will permanently delete the information you need to spend
         funds from this wallet!&lt;/i&gt;  You will only be able to receive
         coins, but not spend them.  Only do this if you have another copy
         of this wallet elsewhere, such as a paper backup or on an offline
         computer with the full wallet. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4590"/>
        <source>Wallet %1 was replaced with a watching-only wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4597"/>
        <source>Wallet %1 was deleted!</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgReplaceWallet</name>
    <message>
        <location filename="qtdialogs.py" line="13774"/>
        <source>
                       &lt;b&gt;You already have this wallet loaded!&lt;/b&gt;&lt;br&gt;
                       You can choose to:&lt;br&gt;
                       - Cancel wallet restore operation&lt;br&gt;
                       - Set new password and fix any errors&lt;br&gt;
                       - Overwrite old wallet (delete comments &amp; labels)&lt;br&gt;
                       </source>
        <translation>
&lt;b&gt;Diese Wallet ist schon geladen!&lt;/b&gt;&lt;br&gt;
Sie können:&lt;br&gt;
-Wallet Wiederherstellung abbrechen&lt;br&gt;
-Neues Passwort setzen und Fehler beheben&lt;br&gt;
-Überschreibe alte Wallet (lösche Kommentare &amp; Kennzeichnung)&lt;br&gt;
</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13789"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13790"/>
        <source>Overwrite</source>
        <translation>Überschreibe</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13791"/>
        <source>Merge</source>
        <translation>Vereine</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13839"/>
        <source>Ripping Meta Data</source>
        <translation>Trenne Meta Daten ab</translation>
    </message>
</context>
<context>
    <name>DlgRequestPayment</name>
    <message>
        <location filename="qtdialogs.py" line="10202"/>
        <source>Other Options &gt;&gt;&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9888"/>
        <source>Copy to Clipboard</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9889"/>
        <source>Copy Raw HTML</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9890"/>
        <source>Copy Raw URL</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9891"/>
        <source>Copy All Text</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9914"/>
        <source>Create a clickable link that you can copy into email or webpage to request a payment.   If the user is running a Bitcoin program that supports &quot;bitcoin:&quot; links, that program will open with all this information pre-filled after they click the link.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9924"/>
        <source>The following Bitcoin desktop applications &lt;i&gt;try&lt;/i&gt; to register themselves with your computer to handle &quot;bitcoin:&quot; links: Armory, Multibit, Electrum</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9928"/>
        <source>This is the text to be shown as the clickable link.  It should usually begin with &quot;Click here...&quot; to reaffirm to the user it is is clickable.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9932"/>
        <source>All amounts are specifed in BTC</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9934"/>
        <source>The person clicking the link will be sending bitcoins to this address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9936"/>
        <source>This will be pre-filled as the label/comment field after the user clicks the link. They can modify it if desired, but you can provide useful info such as contact details, order number, etc, as convenience to them.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9944"/>
        <source>Close</source>
        <translation>Schliessen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9952"/>
        <source>&lt;b&gt;Link Text:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9957"/>
        <source>&lt;b&gt;Address (yours):&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9962"/>
        <source>&lt;b&gt;Request (BTC):&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9966"/>
        <source>&lt;b&gt;Label:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9972"/>
        <source>Copy and paste the following text into email or other document:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9981"/>
        <source>Creating QR Code Please Wait</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9985"/>
        <source>This QR code contains address &lt;b&gt;and&lt;/b&gt; the other payment information shown to the left.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10019"/>
        <source>Create Payment Request Link</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10077"/>
        <source>Amount</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10087"/>
        <source>Message</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10092"/>
        <source>Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10097"/>
        <source>Inputs</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10101"/>
        <source>&lt;font color=&quot;red&quot;&gt;Invalid %1&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10117"/>
        <source>If clicking on the line above does not work, use this payment info:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10119"/>
        <source>&lt;b&gt;Pay to&lt;/b&gt;:<byte value="x9"/>%1&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10121"/>
        <source>&lt;b&gt;Amount&lt;/b&gt;:<byte value="x9"/>%1 BTC&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10123"/>
        <source>&lt;b&gt;Message&lt;/b&gt;:<byte value="x9"/>%1&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10133"/>
        <source>If clicking on the line above does not work, use this payment info:
</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10134"/>
        <source>Pay to:  %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10136"/>
        <source>
Amount:  %1 BTC</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10138"/>
        <source>
Message: %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10142"/>
        <source>&lt;!DOCTYPE HTML PUBLIC &quot;-//W3C//DTD HTML 4.0//EN&quot; &quot;http://www.w3.org/TR/REC-html40/strict.dtd&quot;&gt; &lt;html&gt;&lt;head&gt;&lt;meta name=&quot;qrichtext&quot; content=&quot;1&quot; /&gt;&lt;meta http-equiv=&quot;Content-Type&quot; content=&quot;text/html; charset=utf-8&quot; /&gt;&lt;style type=&quot;text/css&quot;&gt; p, li { white-space: pre-wrap; } &lt;/style&gt;&lt;/head&gt;&lt;body&gt;&lt;p style=&quot; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;&quot;&gt;&lt;!--StartFragment--&gt;&lt;a href=&quot;%1&quot;&gt;&lt;span style=&quot; text-decoration: underline; color:#0000ff;&quot;&gt;%2&lt;/span&gt;&lt;/a&gt;&lt;br /&gt;If clicking on the line above does not work, use this payment info:&lt;br /&gt;&lt;span style=&quot; font-weight:600;&quot;&gt;Pay to&lt;/span&gt;: %3</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10156"/>
        <source>&lt;br /&gt;&lt;span style=&quot; font-weight:600;&quot;&gt;Amount&lt;/span&gt;: %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10159"/>
        <source>&lt;br /&gt;&lt;span style=&quot; font-weight:600;&quot;&gt;Message&lt;/span&gt;: %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10222"/>
        <source>&lt;i&gt;Copied!&lt;/i&gt;</source>
        <translation>&lt;i&gt;Kopiert!&lt;/i&gt;</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10200"/>
        <source>Hide Buttons &lt;&lt;&lt;</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgRestoreFragged</name>
    <message>
        <location filename="qtdialogs.py" line="12759"/>
        <source>&lt;font color=&quot;blue&quot; size=&quot;4&quot;&gt;Testing a
                     Fragmented Backup&lt;/font&gt;</source>
        <translation>&lt;font color=&quot;blue&quot; size=&quot;4&quot;&gt;Ein fragmentiertes
Backup testen&lt;/font&gt;</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12762"/>
        <source>Restore Wallet from Fragments</source>
        <translation>Stelle Wallet aus Fragmenten wieder her</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12764"/>
        <source>
         &lt;b&gt;&lt;u&gt;%1&lt;/u&gt;&lt;/b&gt; &lt;br&gt;&lt;br&gt;
         Use this form to enter all the fragments to be restored.  Fragments
         can be stored on a mix of paper printouts, and saved files.
         If any of the fragments require a SecurePrintâ¢ code,
         you will only have to enter it once, since that code is the same for
         all fragments of any given wallet. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12773"/>
        <source> &lt;br&gt;&lt;br&gt;
            &lt;b&gt;For testing purposes, you may enter more fragments than needed
            and Armory will test all subsets of the entered fragments to verify
            that each one still recovers the wallet successfully.&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12788"/>
        <source>Input Fragments Below:</source>
        <translation>Fragmente nachstehend eintragen:</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12789"/>
        <source>+Frag</source>
        <translation>+Frag</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12790"/>
        <source>-Frag</source>
        <translation>-Frag</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12794"/>
        <source>Encrypt Restored Wallet</source>
        <translation>Verschlüssel wiederhergestellte Wallet</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12803"/>
        <source>Test Backup</source>
        <translation>Teste Backup</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12803"/>
        <source>Restore from Fragments</source>
        <translation>Wiederherstellung durch Fragmente</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12805"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12812"/>
        <source>SecurePrintâ¢ Code:</source>
        <translation>SicherheitsDruck™ Code:</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12853"/>
        <source>Fragments</source>
        <translation>Fragmente</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12855"/>
        <source>Advanced Options</source>
        <translation>Erweiterte Optionen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12870"/>
        <source>Restore wallet from fragments</source>
        <translation>Wallet durch Fragmente wiederherstellen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12888"/>
        <source>Type Data</source>
        <translation>Daten eingeben</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12889"/>
        <source>Load File</source>
        <translation>Datei laden</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12890"/>
        <source>Clear</source>
        <translation>Leeren</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12957"/>
        <source>Load Fragment File</source>
        <translation>Lade Fragment Datei</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12957"/>
        <source>Wallet Fragments (*.frag)</source>
        <translation>Wallet Fragmente (*.frag)</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12965"/>
        <source>File Does Not Exist</source>
        <translation>Datei existiert nicht</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12965"/>
        <source>
            The file you select somehow does not exist...?
            &lt;br&gt;&lt;br&gt;%1&lt;br&gt;&lt;br&gt; Try a different file</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12999"/>
        <source>Fragment Error</source>
        <translation>Fragment Fehler</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12999"/>
        <source>
                  There was an unfixable error in the fragment file:
                  &lt;br&gt;&lt;br&gt; File: %1 &lt;br&gt; Line: %2 &lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13027"/>
        <source>
         &lt;b&gt;Start entering fragments into the table to left...&lt;/b&gt;</source>
        <translation>
&lt;b&gt;Tragen sie die Fragmente in die linke Tabelle ein...&lt;/b&gt;</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13032"/>
        <source>&lt;b&gt;&lt;u&gt;Wallet Being Restored:&lt;/u&gt;&lt;/b&gt;</source>
        <translation>&lt;b&gt;&lt;u&gt;Wallet wird wiederhergestellt:&lt;/u&gt;&lt;/b&gt;</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13034"/>
        <source>&lt;b&gt;Frags Needed:&lt;/b&gt; %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13035"/>
        <source>&lt;b&gt;Wallet:&lt;/b&gt; %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13036"/>
        <source>&lt;b&gt;Fragments:&lt;/b&gt; %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13078"/>
        <source>Mixed fragment types</source>
        <translation>Gemischte Fragment Varianten</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13078"/>
        <source>
            You entered a fragment for a different wallet type.  Please check
            that all fragments are for the same wallet, of the same version,
            and require the same number of fragments.</source>
        <translation>
Es wurde ein Fragment für eine andere Wallet Variante eingegeben.
Bitte überprüfen sie, ob alle Fragmente für die gleiche Wallet und von
der gleichen Version sind, sowie dieselbe Anzahl Fragmente benötigen. </translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13098"/>
        <source>Multiple Walletss</source>
        <translation>Mehrere Wallets</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13098"/>
        <source>
            The fragment you just entered is actually for a different wallet
            than the previous fragments you entered.  Please double-check that
            all the fragments you are entering belong to the same wallet and
            have the &quot;number of needed fragments&quot; (M-value, in M-of-N).</source>
        <translation>
Das eingetragene Fragment ist für eine andere Wallet als die zuvor
eingetragenen. Bitte überprüfen sie, ob alle Fragmente für die gleiche Wallet sind und die gleiche &quot;Anzahl benötigter Fragmente&quot; (M-Wert, in M-von-N).</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13109"/>
        <source>Duplicate Fragment</source>
        <translation>Doppeltes Fragment</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13109"/>
        <source>
            You just input fragment #%1, but that fragment has already been
            entered!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13146"/>
        <source>Invalid Target Compute Time</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13146"/>
        <source>You entered Target Compute Time incorrectly.

Enter: &lt;Number&gt; (ms, s)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13150"/>
        <source>Invalid Max Memory Usage</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13150"/>
        <source>You entered Max Memory Usage incorrectly.

nter: &lt;Number&gt; (kb, mb)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13221"/>
        <source>Verify Wallet ID</source>
        <translation>Überprüfe Wallet ID</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13221"/>
        <source>
         The data you entered corresponds to a wallet with a wallet
         ID:&lt;blockquote&gt;&lt;b&gt;{}&lt;/b&gt;&lt;/blockquote&gt;Does this ID
         match the &quot;Wallet Unique ID&quot; printed on your paper backup?
         If not, click &quot;No&quot; and reenter key and chain-code data
         again.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13237"/>
        <source>Cannot Encrypt</source>
        <translation>Kann nicht verschlüsseln</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13237"/>
        <source>
               You requested your restored wallet be encrypted, but no
               valid passphrase was supplied.  Aborting wallet
               recovery.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13279"/>
        <source>Computing New Addresses</source>
        <translation>Berechne neue Adressen</translation>
    </message>
</context>
<context>
    <name>DlgRestoreSingle</name>
    <message>
        <location filename="qtdialogs.py" line="12191"/>
        <source>
         &lt;b&gt;&lt;u&gt;&lt;font color=&quot;blue&quot; size=&quot;4&quot;&gt;Test a Paper Backup&lt;/font&gt;&lt;/u&gt;&lt;/b&gt;
         &lt;br&gt;&lt;br&gt;
         Use this window to test a single-sheet paper backup.  If your
         backup includes imported keys, those will not be covered by this test.  </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12197"/>
        <source>
         &lt;b&gt;&lt;u&gt;Restore a Wallet from Paper Backup&lt;/u&gt;&lt;/b&gt;
         &lt;br&gt;&lt;br&gt;
         Use this window to restore a single-sheet paper backup.
         If your backup includes extra pages with
         imported keys, please restore the base wallet first, then
         double-click the restored wallet and select &quot;Import Private
         Keys&quot; from the right-hand menu. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12207"/>
        <source>&lt;b&gt;Backup Type:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12209"/>
        <source>Version 1.35 (4 lines)</source>
        <translation>Version 1.35 (4 Zeilen)</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12210"/>
        <source>Version 1.35a (4 lines Unencrypted)</source>
        <translation>Version 1.35a (4 Zeilen unverschlüsselt)</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12211"/>
        <source>Version 1.35a (4 lines + SecurePrintâ¢)</source>
        <translation>Version 1.35a (4 Zeilen + SicherheitsDruck™)</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12212"/>
        <source>Version 1.35c (2 lines Unencrypted)</source>
        <translation>Version 1.35c (2 Zeilen unverschlüsselt)</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12213"/>
        <source>Version 1.35c (2 lines + SecurePrintâ¢)</source>
        <translation>Version 1.35c (2 Zeilen + SicherheitsDruck™)</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12236"/>
        <source>SecurePrintâ¢ Code:</source>
        <translation>SicherheitsDruck™ Code:</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12238"/>
        <source>Root Key:</source>
        <translation>Root Schlüssel:</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12238"/>
        <source>Chaincode:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12255"/>
        <source>Test Backup</source>
        <translation>Teste Backup</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12255"/>
        <source>Restore Wallet</source>
        <translation>Wallet wiederherstellen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12258"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12265"/>
        <source>Encrypt Wallet</source>
        <translation>Wallet verschlüsseln</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12271"/>
        <source>Backup</source>
        <translation>Backup</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12273"/>
        <source>Advanced Options</source>
        <translation>Erweiterte Optionen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12287"/>
        <source>Test Single-Sheet Backup</source>
        <translation>Teste Einseiten Backup</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12289"/>
        <source>Restore Single-Sheet Backup</source>
        <translation>Stelle Einseiten Backup wiederher</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12346"/>
        <source>Invalid Data</source>
        <translation>Ungültige Daten</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12346"/>
        <source>
               There is an error in the data you entered that could not be
               fixed automatically.  Please double-check that you entered the
               text exactly as it appears on the wallet-backup page.  &lt;br&gt;&lt;br&gt;
               The error occured on &lt;font color=&quot;red&quot;&gt;line #%1&lt;/font&gt;.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12359"/>
        <source>Invalid Target Compute Time</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12359"/>
        <source>You entered Target Compute Time incorrectly.

Enter: &lt;Number&gt; (ms, s)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12363"/>
        <source>Invalid Max Memory Usage</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12363"/>
        <source>You entered Max Memory Usage incorrectly.

nter: &lt;Number&gt; (kb, mb)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12369"/>
        <source>
            Detected errors in the data you entered.
            Armory attempted to fix the errors but it is not
            always right.  Be sure to verify the &quot;Wallet Unique ID&quot;
            closely on the next window.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12376"/>
        <source>Errors Corrected</source>
        <translation>Fehler behoben</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12425"/>
        <source>Verify Wallet ID</source>
        <translation>Überprüfe Wallet ID</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12425"/>
        <source>The data you entered corresponds to a wallet with a wallet ID: 

 <byte value="x9"/>
                  %1

Does this ID match the &quot;Wallet Unique ID&quot; 
                  printed on your paper backup?  If not, click &quot;No&quot; and reenter 
                  key and chain-code data again.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12440"/>
        <source>Cannot Encrypt</source>
        <translation>Kann nicht verschlüsseln</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12440"/>
        <source>You requested your restored wallet be encrypted, but no valid passphrase was supplied.  Aborting wallet recovery.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12484"/>
        <source>Computing New Addresses</source>
        <translation>Berechne neue Adressen</translation>
    </message>
</context>
<context>
    <name>DlgRestoreWOData</name>
    <message>
        <location filename="qtdialogs.py" line="12520"/>
        <source>
         &lt;b&gt;&lt;u&gt;&lt;font color=&quot;blue&quot; size=&quot;4&quot;&gt;Test a Watch-Only Wallet Restore
         &lt;/font&gt;&lt;/u&gt;&lt;/b&gt;&lt;br&gt;&lt;br&gt;
         Use this window to test the restoration of a watch-only wallet using
         the wallet's data. You can either type the data on a root data
         printout or import the data from a file.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12527"/>
        <source>
         &lt;b&gt;&lt;u&gt;&lt;font color=&quot;blue&quot; size=&quot;4&quot;&gt;Restore a Watch-Only Wallet
         &lt;/font&gt;&lt;/u&gt;&lt;/b&gt;&lt;br&gt;&lt;br&gt;
         Use this window to restore a watch-only wallet using the wallet's
         data. You can either type the data on a root data printout or import
         the data from a file.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12535"/>
        <source>Watch-Only Root ID:</source>
        <translation>Nur-beobachten Root ID:</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12543"/>
        <source>Data:</source>
        <translation>Daten:</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12562"/>
        <source>Test Backup</source>
        <translation>Teste Backup</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12562"/>
        <source>Restore Wallet</source>
        <translation>Wallet wiederherstellen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12563"/>
        <source>Load From Text File</source>
        <translation>Lade aus Text Datei</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12565"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12592"/>
        <source>Test Watch-Only Wallet Backup</source>
        <translation>Teste nur-beobachten Wallet Backup</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12594"/>
        <source>Restore Watch-Only Wallet Backup</source>
        <translation>Nur-beobachten Wallet Backup wiederherstellen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12604"/>
        <source>Import Wallet File</source>
        <translation>Lese Wallet Datei ein</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12604"/>
        <source>Root Pubkey Text Files (*.rootpubkey)</source>
        <translation>Root publik Schlüssel Text Dateien (*.rootpubkey)</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12687"/>
        <source>Invalid Data</source>
        <translation>Ungültige Daten</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12657"/>
        <source>
               There is an error in the root ID you entered that could not
               be fixed automatically.  Please double-check that you entered the
               text exactly as it appears on the wallet-backup page.&lt;br&gt;&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12687"/>
        <source>
               There is an error in the root data you entered that could not be
               fixed automatically.  Please double-check that you entered the
               text exactly as it appears on the wallet-backup page.  &lt;br&gt;&lt;br&gt;
               The error occured on &lt;font color=&quot;red&quot;&gt;line #%1&lt;/font&gt;.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12720"/>
        <source>Wallet Already Exists</source>
        <translation>Wallet besteht bereits</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12720"/>
        <source>The
                             wallet already exists and will not be
                             replaced.</source>
        <translation>Die
Wallet besteht bereits und wird nicht
ersetzt.</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12727"/>
        <source>Verify Wallet ID</source>
        <translation>Überprüfe Wallet ID</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12727"/>
        <source>The data you entered corresponds to a wallet with a wallet ID: 

<byte value="x9"/>%1

Does this ID match the &quot;Wallet Unique ID&quot; you intend to restore? If not, click &quot;No&quot; and enter the key and chain-code data again.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12742"/>
        <source>Computing New Addresses</source>
        <translation>Berechne neue Adressen</translation>
    </message>
</context>
<context>
    <name>DlgSelectMultiSigOption</name>
    <message>
        <location filename="MultiSigDialogs.py" line="3849"/>
        <source>Create/Manage lockboxes</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3851"/>
        <source>Fund a lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3852"/>
        <source>Spend from a lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3854"/>
        <source>
         &lt;font color=&quot;%1&quot; size=5&gt;&lt;b&gt;Multi-Sig Lockboxes
         [EXPERIMENTAL]&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3859"/>
        <source>
         The buttons below link you to all the functionality needed to 
         create, fund and spend from multi-sig &quot;lockboxes.&quot;  This 
         includes turning multiple wallets into a multi-factor lock-box
         for your personal coins, or can be used for escrow between
         multiple parties, using the Bitcoin network itself to hold the
         escrow.
         &lt;br&gt;&lt;br&gt;
         &lt;b&gt;&lt;u&gt;IMPORTANT:&lt;/u&gt;&lt;/b&gt;  If you are using an lockbox that requires
         being funded by multiple parties simultaneously, you should 
         &lt;b&gt;&lt;u&gt;not&lt;/u&gt; &lt;/b&gt; use regular transactions to do the funding.  
         You should use the third button labeled &quot;Fund a multi-sig lockbox&quot; 
         to collect funding promises into a single transaction, to limit 
         the ability of any party to scam you.  Read more about it by
         clicking [NO LINK YET]  (if the above doesn't hold, you can use
         the regular &quot;Send Bitcoins&quot; dialog to fund the lockbox).</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3877"/>
        <source>
         Collect public keys to create an &quot;address&quot; that can be used 
         to send funds to the multi-sig container</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3883"/>
        <source>
         Send money to an lockbox simultaneously with other 
         parties involved in the lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3886"/>
        <source>
         Collect signatures to authorize transferring money out of 
         a multi-sig lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3939"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3950"/>
        <source>Multi-Sig Lockboxes</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgSelectPublicKey</name>
    <message>
        <location filename="MultiSigDialogs.py" line="2148"/>
        <source>
         &lt;center&gt;&lt;font size=4&gt;&lt;b&gt;&lt;u&gt;Select Public Key for Lockbox 
         Creation&lt;/u&gt;&lt;/b&gt;&lt;/font&gt;&lt;/center&gt;
         &lt;br&gt;
         Lockbox creation requires &lt;b&gt;public keys&lt;/b&gt; not the regular Bitcoin
         addresses most users are accustomed to.  A public key is much longer
         than a regular bitcoin address, usually starting with &quot;02&quot;, &quot;03&quot; or
         &quot;04&quot;.  Once you have selected a public key, send it to the lockbox 
         organizer (person or device).  The organizer will create the lockbox 
         which then must be imported by all devices that will track the funds
         and/or sign transactions.
         &lt;br&gt;&lt;br&gt;
         It is recommended that you select a &lt;i&gt;new&lt;/i&gt; key from one of your
         wallets that will not be used for any other purpose.
         You &lt;u&gt;can&lt;/u&gt; use a public key from a watching-only wallet (for 
         an offline wallet), but you will have to sign the transactions the
         same way you would a regular offline transaction.  Additionally the 
         offline computer will need to have Armory version 0.92 or later.
         &lt;br&gt;&lt;br&gt;
         &lt;b&gt;&lt;font color=&quot;%1&quot;&gt;BACKUP WARNING&lt;/b&gt;&lt;/b&gt;:
         It is highly recommended that you select a public key from a
         wallet for which you have good backups!  If you are creating a lockbox
         requiring the same number of signatures as there are authorities 
         (such as 2-of-2 or 3-of-3), the loss of the wallet &lt;u&gt;will&lt;/u&gt; lead 
         to loss of lockbox funds!  
         </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2175"/>
        <source>Select Public Key:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2176"/>
        <source>Notes or Contact Info:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2177"/>
        <source>
         If multiple people will be part of this lockbox, you should 
         specify name and contact info in the box below, which will be
         available to all parties that import the finalized lockbox.
         &lt;br&gt;&lt;br&gt;
         If this lockbox will be shared among devices you own (such as for
         personal savings), specify information that helps you identify which
         device is associated with this public key.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2222"/>
        <source>Continue</source>
        <translation>Fortfahren</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2223"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2237"/>
        <source>Select Public Key for Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2255"/>
        <source>Invalid Public Key</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2255"/>
        <source>
            You must enter a public key into the box, &lt;b&gt;not&lt;/b&gt; a regular 
            Bitcoin address that most users are accustomed to.  A public key 
            is much longer than a Bitcoin address, and always starts with 
            &quot;02&quot;, &quot;03&quot; or &quot;04&quot;.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2276"/>
        <source>Export Public Key for Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2277"/>
        <source>
         The text below includes both the public key and the notes/contact info
         you entered.  Please send this text to the organizer (person or device) 
         to be used to create the lockbox.  This data is &lt;u&gt;not&lt;/u&gt; sensitive 
         and it is appropriate be sent via email or transferred via USB storage.
         </source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgSendBitcoins</name>
    <message>
        <location filename="qtdialogs.py" line="5006"/>
        <source>Send Bitcoins</source>
        <translation>Bitcoins versenden</translation>
    </message>
</context>
<context>
    <name>DlgSetComment</name>
    <message>
        <location filename="qtdialogs.py" line="4302"/>
        <source>Modify Comment</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4312"/>
        <source>Change %1 %2:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4313"/>
        <source>Change %1:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4314"/>
        <source>Add %2 %2:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4315"/>
        <source>Add %1:</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgSetLongDescr</name>
    <message>
        <location filename="MultiSigDialogs.py" line="336"/>
        <source>
               &lt;b&gt;&lt;u&gt;Set Extended Lockbox Details&lt;/u&gt;&lt;/b&gt;
               &lt;br&gt;&lt;br&gt;
               Use this space to store any extended information about this
               multi-sig lockbox, such as contact information of other
               parties, references to contracts, etc.  Keep in mind that this
               field will be included when this lockbox is shared with others,
               so you should include your own contact information, as well as
               avoid putting any sensitive data in here</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="348"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="356"/>
        <source>Edit Lockbox Description</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="393"/>
        <source>
            Using the &lt;font color=&quot;%1&quot;&gt;&lt;b&gt;%2&lt;/b&gt;&lt;/font&gt; public keys above,
            a multi-sig lockbox will be created requiring
            &lt;font color=&quot;%3&quot;&gt;&lt;b&gt;%4&lt;/b&gt;&lt;/font&gt; signatures to spend
            money.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="488"/>
        <source>Missing Name</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="488"/>
        <source>
            Lockboxes cannot be saved without a name (at the top of 
            the public key list).  It is also recommended to set the
            extended information next to it, for documenting the purpose
            of the lockbox.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="502"/>
        <source>Not Enough Keys</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="502"/>
        <source>
               You specified less than &lt;b&gt;%1&lt;/b&gt; public keys.  Please enter
               a public key into every field before continuing.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="517"/>
        <source>Invalid Public Key</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="517"/>
        <source>
               The data specified for public key &lt;b&gt;%1&lt;/b&gt; is not valid.
               Please double-check the data was entered correctly.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="533"/>
        <source>Empty Name/ID Field</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="533"/>
        <source> 
               You did not specify a comment/label for one or more 
               public keys.  Other devices/parties may not be able to 
               identify them.  If this is a multi-party
               lockbox, it is recommended you put in contact information
               for each party, such as name, email and/or phone number.
               &lt;br&gt;&lt;br&gt;
               Continue with some fields blank?
               &lt;br&gt;(click &quot;No&quot; to go back and finish filling in the form)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="562"/>
        <source>Different Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="562"/>
        <source>
               You originally loaded lockbox (%1) but the edits you made
               have caused it to become a new/different lockbox (%2).
               Changing the M-value, N-value, or any of the public keys 
               will result in a new lockbox, unrelated to the original.
               &lt;br&gt;&lt;br&gt;
               &lt;b&gt;If you click &quot;Ok&quot; a new lockbox will be created&lt;/b&gt; instead
               of replacing the original.  If you do not need the original,
               you can go the lockbox browser and manually remove it.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="577"/>
        <source>Non-Standard to Spend</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="577"/>
        <source>
            If you are running any Bitcoin Core version earlier than 0.9.3
            all spending transactions from this lockbox
            will be rejected as non-standard.  There will be no problem sending coins  
            &lt;u&gt;to&lt;/u&gt; the lockbox, but subsequent spends &lt;u&gt;from&lt;/u&gt; the 
            lockbox will require you to upgrade Bitcoin Core to at least 0.9.3 or later.  
            &lt;br&gt;&lt;br&gt;
            Do you wish to continue creating the lockbox, anyway?</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgSettings</name>
    <message>
        <location filename="qtdialogs.py" line="8717"/>
        <source>
         Let Armory run Bitcoin Core/bitcoind in the background</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8728"/>
        <source>Bitcoin Core/bitcoind management is not available on Mac/OSX</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8738"/>
        <source>&lt;b&gt;Bitcoin Software Management&lt;/b&gt;&lt;br&gt;&lt;br&gt;By default, Armory will manage the Bitcoin engine/software in the background.  You can choose to manage it yourself, or tell Armory about non-standard installation configuration.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8751"/>
        <source>Bitcoin Install Dir:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8752"/>
        <source>Bitcoin Home Dir:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8753"/>
        <source>Leave blank to have Armory search default locations for your OS</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8755"/>
        <source>Leave blank to use default datadir (%1)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8782"/>
        <source>
         Skip online check on startup (assume internet is available, do
         not check)</source>
        <translation>
Überspringe online Check beim Start (vermute bestehende
Internetverbindung, nicht überprüfen)</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8798"/>
        <source>&lt;b&gt;Privacy Settings&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8805"/>
        <source>
         If you are going to use Armory and Bitcoin Core with a proxy (such
         as Tor), you should disable all Armory communications that might operate
         outside the proxy.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8813"/>
        <source>
         Enable settings for proxies/Tor</source>
        <translation>
Aktiviere Einstellungen für Proxy/Tor</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8825"/>
        <source>
         &lt;b&gt;Set Armory as default URL handler&lt;/b&gt;</source>
        <translation>
&lt;b&gt;Setze Armory als Standardanwendung für URL&lt;/b&gt;</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8827"/>
        <source>
         Set Armory to be the default when you click on &quot;bitcoin:&quot;
         links in your browser or in emails.
         You can test if your operating system is supported by clicking
         on a &quot;bitcoin:&quot; link right after clicking this button.</source>
        <translation>
Setze Armory als Standard für Klicks auf &quot;bitcoin:&quot; Links im
Browser oder in E-Mails.
Ob ihr Betriebssystem unterstützt wird können sie an einem &quot;bitcoin:&quot;
Link testen nachdem sie auf diesen Knopf drücken.</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8833"/>
        <source>Set Armory as Default</source>
        <translation>Setze Armory als Standard</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8836"/>
        <source>
         Check whether Armory is the default handler at startup</source>
        <translation>
Überprüfe ob Armory die Standardanwendung ist beim Start</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8843"/>
        <source>Registered</source>
        <translation>Registriert</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8843"/>
        <source>
            Armory just attempted to register itself to handle &quot;bitcoin:&quot;
            links, but this does not work on all operating systems.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8851"/>
        <source>
         &lt;b&gt;Default fee to include with transactions:&lt;/b&gt;&lt;br&gt;</source>
        <translation>
&lt;b&gt;Standard Gebühr für Transaktionen:&lt;/b&gt;&lt;br&gt;</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8853"/>
        <source>
         Fees go to users that contribute computing power to keep the
         Bitcoin network secure.  It also increases the priority of your
         transactions so they confirm faster (%1 BTC is standard).</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8858"/>
        <source>
         NOTE: some transactions will require a certain fee
         regardless of your settings -- in such cases
         you will be prompted to include the correct
         value or cancel the transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8873"/>
        <source>
         &lt;b&gt;Minimize to System Tray&lt;/b&gt;
         &lt;br&gt;
         You can have Armory automatically minimize itself to your system
         tray on open or close.  Armory will stay open but run in the
         background, and you will still receive notifications.  Access Armory
         through the icon on your system tray.
         &lt;br&gt;&lt;br&gt;
         If select &quot;Minimize on close&quot;, the 'x' on the top window bar will
         minimize Armory instead of exiting the application.  You can always use
         &lt;i&gt;&quot;File&quot;&lt;/i&gt; -&gt; &lt;i&gt;&quot;Quit Armory&quot;&lt;/i&gt; to actually close it.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8886"/>
        <source>Minimize to system tray on open</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8891"/>
        <source>Minimize to system tray on close</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8904"/>
        <source>&lt;b&gt;Enable notifications from the system-tray:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8905"/>
        <source>Bitcoins Received</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8906"/>
        <source>Bitcoins Sent</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8907"/>
        <source>Bitcoin Core/bitcoind disconnected</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8908"/>
        <source>Bitcoin Core/bitcoind reconnected</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8912"/>
        <source>&lt;b&gt;Sorry!  Notifications are not available on your version of OS X.&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8937"/>
        <source>&lt;b&gt;Preferred Date Format&lt;b&gt;:&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8938"/>
        <source>You can specify how you would like dates to be displayed using percent-codes to represent components of the date.  The mouseover text of the &quot;(?)&quot; icon shows the most commonly used codes/symbols.  The text next to it shows how &quot;%1&quot; would be shown with the specified format.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8949"/>
        <source>Use any of the following symbols:&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8963"/>
        <source>Reset to Default</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8981"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8982"/>
        <source>Save</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9003"/>
        <source>&lt;b&gt;Armory user mode:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9011"/>
        <source>&lt;b&gt;Preferred Language&lt;b&gt;:&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9012"/>
        <source>Specify which language you would like Armory to be displayed in.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9133"/>
        <source>General</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9139"/>
        <source>Fee &amp; Change</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9153"/>
        <source>Armory Settings</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9190"/>
        <source>&lt;b&gt;Fee&lt;br&gt;&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9192"/>
        <source>Auto fee/byte</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9194"/>
        <source>
      Fetch fee/byte from local Bitcoin node. 
      Defaults to manual fee/byte on failure. 
      </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9199"/>
        <source>Manual fee/byte</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9202"/>
        <source>
      Values in satoshis/byte
      </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9206"/>
        <source>Flat fee</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9209"/>
        <source>
      Values in BTC
      </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9213"/>
        <source>Auto-adjust fee/byte for better privacy</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9215"/>
        <source>
      Auto-adjust fee may increase your total fee using the selected fee/byte rate
      as its basis in an attempt to align the amount of digits after the decimal
      point between your spend values and change value.
      
      &lt;br&gt;&lt;br&gt;
      The purpose of this obfuscation technique is to make the change output
      less obvious. 
      
      &lt;br&gt;&lt;br&gt;
      The auto-adjust fee feature only applies to fee/byte options
      and does not inflate your fee by more that 10% of its original value.    
      </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9279"/>
        <source>&lt;b&gt;Change Address Type&lt;br&gt;&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9281"/>
        <source>Auto change</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9283"/>
        <source>
      Change address type will match the address type of recipient
      addresses. &lt;br&gt;
      
      Favors P2SH when recipients are heterogenous. &lt;br&gt;
      
      Will create nested SegWit change if inputs are SegWit and 
      recipient are P2SH. &lt;br&gt;&lt;br&gt;
      
      &lt;b&gt;Pre 0.96 Armory cannot spend from P2SH address types&lt;/b&gt;
      </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9295"/>
        <source>Force P2PKH</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9298"/>
        <source>Force P2SH-P2PK</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9301"/>
        <source>Force P2SH-P2WPKH</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9303"/>
        <source>
      Defaults back to P2SH-P2PK if SegWit is not enabled
      </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9359"/>
        <source>Invalid Path</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9342"/>
        <source>The path you specified for the Bitcoin software installation does not exist.  Please select the directory that contains %1 or leave it blank to have Armory search the default location for your operating system</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9359"/>
        <source>The path you specified for the Bitcoin software home directory does not exist.  Only specify this directory if you use a non-standard &quot;-datadir=&quot; option when running Bitcoin Core or bitcoind.  If you leave this field blank, the following path will be used: &lt;br&gt;&lt;br&gt; %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9382"/>
        <source>Invalid Amount</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9382"/>
        <source>The default fee specified could not be understood.  Please specify in BTC with no more than 8 decimal places.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9441"/>
        <source>&quot;Standard&quot; is for users that only need the core set of features to send and receive bitcoins.  This includes maintaining multiple wallets, wallet encryption, and the ability to make backups of your wallets.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9447"/>
        <source>&quot;Advanced&quot; mode provides extra Armory features such as private key importing &amp; sweeping, message signing, and the offline wallet interface.  But, with advanced features come advanced risks...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9453"/>
        <source>&quot;Expert&quot; mode is similar to &quot;Advanced&quot; but includes access to lower-level info about transactions, scripts, keys and network protocol.  Most extra functionality is geared towards Bitcoin software developers.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9464"/>
        <source>Sample: </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="9467"/>
        <source>Sample: [[invalid date format]]</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgShowKeyList</name>
    <message>
        <location filename="qtdialogs.py" line="5253"/>
        <source>The textbox below shows all keys that are part of this wallet,which includes both permanent keys and imported keys.  If yousimply want to backup your wallet and you have no imported keysthen all data below is reproducible from a plain paper backup.&lt;br&gt;&lt;br&gt;If you have imported addresses to backup, and/or youwould like to export your private keys to anotherwallet service or application, then you can save this datato disk, or copy&amp;paste it into the other application.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5263"/>
        <source>&lt;br&gt;&lt;br&gt;&lt;font color=&quot;red&quot;&gt;Warning:&lt;/font&gt; The text box below containsthe plaintext (unencrypted) private keys for each ofthe addresses in this wallet.  This information can be usedto spend the money associated with those addresses, so pleaseprotect it like you protect the rest of your wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5289"/>
        <source>Address String</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5290"/>
        <source>Hash160</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5291"/>
        <source>Private Key (Encrypted)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5292"/>
        <source>Private Key (Plain Hex)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5293"/>
        <source>Private Key (Plain Base58)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5294"/>
        <source>Public Key (BE)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5295"/>
        <source>Chain Index</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5313"/>
        <source>Imported Addresses Only</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5314"/>
        <source>Include Unused (Address Pool)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5315"/>
        <source>Include Paper Backup Root</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5316"/>
        <source>Omit spaces in key data</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5354"/>
        <source>&lt;&lt;&lt; Go Back</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5355"/>
        <source>Save to File...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5356"/>
        <source>Copy to Clipboard</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5389"/>
        <source>All Wallet Keys</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5494"/>
        <source>Plaintext Private Keys</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5494"/>
        <source>&lt;font color=&quot;red&quot;&gt;&lt;b&gt;REMEMBER:&lt;/b&gt;&lt;/font&gt; The data youare about to save contains private keys.  Please make surethat only trusted persons will have access to this file.&lt;br&gt;&lt;br&gt;Are you sure you want to continue?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5505"/>
        <source>Save Key List</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5505"/>
        <source>Text Files (*.txt)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5505"/>
        <source>keylist_%1_.txt</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5519"/>
        <source>&lt;i&gt;Copied!&lt;/i&gt;</source>
        <translation>&lt;i&gt;Kopiert!&lt;/i&gt;</translation>
    </message>
</context>
<context>
    <name>DlgShowKeys</name>
    <message>
        <location filename="qtdialogs.py" line="3830"/>
        <source>
            &lt;font color=%1&gt;&lt;b&gt;Warning:&lt;/b&gt; the unencrypted private keys
            for this address are shown below.  They are &quot;private&quot; because
            anyone who obtains them can spend the money held
            by this address.  Please protect this information the
            same as you protect your wallet.&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3848"/>
        <source>Key Data for address: &lt;b&gt;%1&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3859"/>
        <source>
            The raw form of the private key for this address.  It is
            32-bytes of randomly generated data</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3862"/>
        <source>Private Key (hex,%1):</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3864"/>
        <source>&lt;i&gt;[[ No Private Key in Watching-Only Wallet ]]&lt;/i&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3868"/>
        <source>&lt;i&gt;[[ ENCRYPTED ]]&lt;/i&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3872"/>
        <source>
               This is a more compact form of the private key, and includes
               a checksum for error detection.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3875"/>
        <source>Private Key (Base58):</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3882"/>
        <source>
               The raw public key data.  This is the X-coordinate of
               the Elliptic-curve public key point.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3885"/>
        <source>Public Key X (%1):</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3890"/>
        <source>
               The raw public key data.  This is the Y-coordinate of
               the Elliptic-curve public key point.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3893"/>
        <source>Public Key Y (%1):</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3901"/>
        <source>%1 (Network: %2 / Checksum: %3)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3904"/>
        <source>This is the hexadecimal version if the address string</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3906"/>
        <source>Public Key Hash:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3940"/>
        <source>Address Key Information</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgShowTestResults</name>
    <message>
        <location filename="qtdialogs.py" line="13348"/>
        <source>
            The total number of fragment subsets (%1) is too high
            to test and display.  Instead, %2 subsets were tested
            at random.  The results are below </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13353"/>
        <source>
            For the fragments you entered, there are a total of
            %1 possible subsets that can restore your wallet.
            The test results for all subsets are shown below</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13360"/>
        <source>
         The wallet ID is computed from the first
         address in your wallet based on the root key data (and the
         &quot;chain code&quot;).  Therefore, a matching wallet ID proves that
         the wallet will produce identical addresses.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13373"/>
        <source>
            Fragments &lt;b&gt;%1&lt;/b&gt; and &lt;b&gt;%2&lt;/b&gt; produce a
            wallet with ID &quot;&lt;b&gt;%3&lt;/b&gt;&quot; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13395"/>
        <source>Ok</source>
        <translation>Ok</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13407"/>
        <source>Fragment Test Results</source>
        <translation>Fragment Test Resultate</translation>
    </message>
</context>
<context>
    <name>DlgSignBroadcastOfflineTx</name>
    <message>
        <location filename="qtdialogs.py" line="5206"/>
        <source>Review Offline Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5209"/>
        <source>Sign or Broadcast Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5212"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
</context>
<context>
    <name>DlgSimpleBackup</name>
    <message>
        <location filename="qtdialogs.py" line="11184"/>
        <source>
         &lt;b&gt;Protect Your Bitcoins -- Make a Wallet Backup!&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11187"/>
        <source>
         A failed hard-drive or forgotten passphrase will lead to
         &lt;u&gt;permanent loss of bitcoins&lt;/u&gt;!  Luckily, Armory wallets only
         need to be backed up &lt;u&gt;one time&lt;/u&gt;, and protect you in both
         of these events.   If you've ever forgotten a password or had
         a hardware failure, make a backup! </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11195"/>
        <source>
         Use a printer or pen-and-paper to write down your wallet &quot;seed.&quot; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11197"/>
        <source>Make Paper Backup</source>
        <translation>Papier Backup machen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11200"/>
        <source>
         Create an unencrypted copy of your wallet file, including imported
         addresses.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11203"/>
        <source>Make Digital Backup</source>
        <translation>Digitales Backup machen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11206"/>
        <source> </source>
        <translation> </translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11207"/>
        <source>See Other Backup Options</source>
        <translation>Andere Backup Optionen anzeigen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11250"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11260"/>
        <source>Backup Options</source>
        <translation>Backup Optionen</translation>
    </message>
</context>
<context>
    <name>DlgSimulfundSelect</name>
    <message>
        <location filename="MultiSigDialogs.py" line="1986"/>
        <source>
         &lt;font color=&quot;%1&quot; size=4&gt;&lt;b&gt;Simultaneous Lockbox
         Funding&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1990"/>
        <source>
         To have multiple parties simultaneously fund a lockbox, each party
         will need to create a &quot;promissory note,&quot; and any other party will
         collect all of them to create a single simulfunding transaction.
         This transaction will be signed by all parties after reviewing that
         it meets their expectations.  This process guarantees that either 
         all parties commit funds simultaneously, or no one does.  The
         signature that you provide using this interface is only valid if 
         all the other funding commitments are also signed.
         &lt;br&gt;&lt;br&gt;
         If you are both creating a promissory note and merging all the 
         notes together, you should first create the promissory note and
         save it to disk or copy it to your clipboard.  Once all other 
         funding commitments have been received, open this dialog again 
         and load all of them at once.  Sign for your contribution and
         send the result to all the other parties.
         &lt;br&gt;&lt;br&gt;
         You are currently handling a simulfunding operation for lockbox:
         &lt;br&gt;%1.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2011"/>
        <source>Create Promissory Note</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2012"/>
        <source>Collect and Merge Notes</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2013"/>
        <source>Sign Simulfunding Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2014"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2017"/>
        <source>
            Create a commitment to a simulfunding transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2021"/>
        <source>
            Note creation is not available when offline.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2024"/>
        <source>
         Collect multiple promissory notes into a single simulfunding
         transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2028"/>
        <source>
         Review and signed a simulfunding transaction (after all promissory
         notes have been collected)</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgSpendFromLockbox</name>
    <message>
        <location filename="MultiSigDialogs.py" line="1911"/>
        <source>
         To spend from a multi-sig lockbox, one party/device must create
         a proposed spending transaction, then all parties/devices must
         review and sign that transaction.  Once it has enough signatures,
         any device, can broadcast the transaction to the network.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1917"/>
        <source>Create Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1918"/>
        <source>Review and Sign</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1919"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1922"/>
        <source>
            I am creating a new proposed spending transaction and will pass
            it to each party or device that needs to sign it</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1927"/>
        <source>
            Transaction creation is not available when offline.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="1930"/>
        <source>
         Another party or device created the transaction, I just need 
         to review and sign it.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgTxFeeOptions</name>
    <message>
        <location filename="qtdialogs.py" line="5544"/>
        <source>
         Transaction fees go to people who contribute processing power to
         the Bitcoin network to process transactions and keep it secure.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="5547"/>
        <source>
         Nearly all transactions are guaranteed to be
         processed if a fee of 0.0005 BTC is included (less than $0.01 USD).  You
         will be prompted for confirmation if a higher fee amount is required for
         your transaction.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgUniversalRestoreSelect</name>
    <message>
        <location filename="qtdialogs.py" line="12052"/>
        <source>
         &lt;b&gt;&lt;u&gt;Restore Wallet from Backup&lt;/u&gt;&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12054"/>
        <source>You can restore any kind of backup ever created by Armory using
         one of the options below.  If you have a list of private keys
         you should open the target wallet and select &quot;Import/Sweep
         Private Keys.&quot;  </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12059"/>
        <source>I am restoring a...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12061"/>
        <source>Single-Sheet Backup (printed)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12062"/>
        <source>Fragmented Backup (incl. mix of paper and files)</source>
        <translation>Fragmentiertes Backup (einschl. Gemisch aus Papier und Dateien)</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12063"/>
        <source>Import digital backup or watching-only wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12064"/>
        <source>Import watching-only wallet data</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12065"/>
        <source>This is a test recovery to make sure my backup works</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12079"/>
        <source>Continue</source>
        <translation>Fortfahren</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12080"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
</context>
<context>
    <name>DlgUnlockWallet</name>
    <message>
        <location filename="qtdialogs.py" line="63"/>
        <source>Enter your passphrase to unlock this wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="64"/>
        <source>Passphrase:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="70"/>
        <source>Unlock</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="71"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="88"/>
        <source>Using a visual keyboard to enter your passphrase protects you against simple keyloggers.   Scrambling makes it difficult to use, but prevents even loggers that record mouse clicks.</source>
        <translation>Die Benutzung einer optischen Tastatur, schützt Sie gegen einfache Keylogger. Die zufällige Anordnung erschwert die Benutzung, schützt jedoch gegen Logger, die Mausklicks aufzeichnen. </translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="95"/>
        <source>Regular Keyboard</source>
        <translation>Standardtastatur</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="96"/>
        <source>Scrambled (Simple)</source>
        <translation>Zufällige Anordnung (Einfach)</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="97"/>
        <source>Scrambled (Dynamic)</source>
        <translation>Zufällige Anordnung (Dynamisch)</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="163"/>
        <source>Show Keyboard &gt;&gt;&gt;</source>
        <translation>Zeige Tastatur &gt;&gt;&gt;</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="161"/>
        <source>Hide Keyboard &lt;&lt;&lt;</source>
        <translation>Verstecke Tastatur &lt;&lt;&lt;</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="178"/>
        <source>SHIFT</source>
        <translation>SHIFT</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="179"/>
        <source>SPACE</source>
        <translation>SPACE</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="180"/>
        <source>DEL</source>
        <translation>DEL</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="316"/>
        <source>Unlocking Wallet</source>
        <translation>Entsperre Wallet</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="326"/>
        <source>Invalid Passphrase</source>
        <translation>Ungültiges Passwort</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="326"/>
        <source>That passphrase is not correct!</source>
        <translation>Das Passwort ist nicht korrekt!</translation>
    </message>
</context>
<context>
    <name>DlgUriCopyAndPaste</name>
    <message>
        <location filename="qtdialogs.py" line="10465"/>
        <source>Copy and paste a raw bitcoin URL string here.  A valid string starts with &quot;bitcoin:&quot; followed by a bitcoin address.&lt;br&gt;&lt;br&gt;You should use this feature if there is a &quot;bitcoin:&quot; link in a webpage or email that does not load Armory when you click on it.  Instead, right-click on the link and select &quot;Copy Link Location&quot; then paste it into the box below. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10481"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="10482"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
</context>
<context>
    <name>DlgVerifySweep</name>
    <message>
        <location filename="qtdialogs.py" line="3401"/>
        <source>
            You are about to &lt;i&gt;sweep&lt;/i&gt; all funds from the specified address
            to your wallet.  Please confirm the action:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3407"/>
        <source>(Fee: %1)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3413"/>
        <source>      From %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3414"/>
        <source>      To %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3415"/>
        <source>      Total &lt;b&gt;%1&lt;/b&gt; BTC %2</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3418"/>
        <source>Are you sure you want to execute this transaction?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="3434"/>
        <source>Confirm Sweep</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgWODataPrintBackup</name>
    <message>
        <location filename="qtdialogs.py" line="11452"/>
        <source>
         &lt;b&gt;&lt;u&gt;Print Watch-Only Wallet Root&lt;/u&gt;&lt;/b&gt;&lt;br&gt;&lt;br&gt;
         The lines below are sufficient to calculate public keys
         for every private key ever produced by the full wallet.
         Importing this data to an online computer is sufficient
         to receive and verify transactions, and monitor balances,
         but without the ability to spend the funds.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11536"/>
        <source>
         &lt;b&gt;&lt;font size=4&gt;&lt;font color=&quot;#aa0000&quot;&gt;WARNING:&lt;/font&gt;  &lt;u&gt;This is not
         a wallet backup!&lt;/u&gt;&lt;/font&gt;&lt;/b&gt;
         &lt;br&gt;&lt;br&gt;Please make a regular digital or paper backup of your wallet
         of your wallet to keep it protected!  This data simply lets you
         monitor the funds in this wallet but gives you no ability to move any
         funds.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11566"/>
        <source>
         The following five lines are sufficient to reproduce all public
         keys matching the private keys produced by the full wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="11605"/>
        <source>
         The following QR code is for convenience only.  It contains the
         exact same data as the five lines above.  If you copy this data
         by hand, you can safely ignore this QR code. </source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgWalletDetails</name>
    <message>
        <location filename="qtdialogs.py" line="1512"/>
        <source>Change Wallet Labels</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1519"/>
        <source>Change or Remove Passphrase</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1521"/>
        <source>Encrypt Wallet</source>
        <translation>Wallet verschlüsseln</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1526"/>
        <source>Send Bitcoins</source>
        <translation>Bitcoins versenden</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1527"/>
        <source>Receive Bitcoins</source>
        <translation>Bitcoins erhalten</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1528"/>
        <source>Import/Sweep Private Keys</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1529"/>
        <source>Remove Imported Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1531"/>
        <source>Export Watching-Only %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1532"/>
        <source>&lt;b&gt;Backup This Wallet&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1533"/>
        <source>Delete/Remove Wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1550"/>
        <source>&lt;u&gt;&lt;/u&gt;Send bitcoins to other users, or transfer 
                             between wallets</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1553"/>
        <source>&lt;u&gt;&lt;/u&gt;If you have a full-copy of this wallet 
                                on another computer, you can prepare a 
                                transaction, to be signed by that computer.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1556"/>
        <source>&lt;u&gt;&lt;/u&gt;Get a new address from this wallet for receiving 
                             bitcoins.  Right click on the address list below 
                             to copy an existing address.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1559"/>
        <source>&lt;u&gt;&lt;/u&gt;Import or &quot;Sweep&quot; an address which is not part 
                             of your wallet.  Useful for VanityGen addresses 
                             and redeeming Casascius physical bitcoins.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1562"/>
        <source>&lt;u&gt;&lt;/u&gt;Permanently delete an imported address from 
                             this wallet.  You cannot delete addresses that 
                             were generated natively by this wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1566"/>
        <source>&lt;u&gt;&lt;/u&gt;Export a copy of this wallet that can 
                             only be used for generating addresses and 
                             monitoring incoming payments.  A watching-only 
                             wallet cannot spend the funds, and thus cannot 
                             be compromised by an attacker</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1571"/>
        <source>&lt;u&gt;&lt;/u&gt;See lots of options for backing up your wallet 
                             to protect the funds in it.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1573"/>
        <source>&lt;u&gt;&lt;/u&gt;Permanently delete this wallet, or just delete 
                            the private keys to convert it to a watching-only 
                            wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1579"/>
        <source>&lt;u&gt;&lt;/u&gt;Add/Remove/Change wallet encryption settings.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1647"/>
        <source>Total funds if all current transactions are confirmed.  
            Value appears gray when it is the same as your spendable funds.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1650"/>
        <source>Funds that can be spent &lt;i&gt;right now&lt;/i&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1652"/>
        <source>Funds that have less than 6 confirmations</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1679"/>
        <source>&lt;b&gt;Addresses in Wallet:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1681"/>
        <source>&lt;&lt;&lt; Go Back</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1700"/>
        <source>Wallet Properties</source>
        <translation>Wallet Eigenschaften</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1713"/>
        <source>Wallet Backup</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1713"/>
        <source>&lt;b&gt;&lt;font color=&quot;red&quot; size=4&gt;Please backup your wallet!&lt;/font&gt;&lt;/b&gt; &lt;br&gt;&lt;br&gt;Making a paper backup will guarantee you can recover your coins at &lt;a&gt;any time in the future&lt;/a&gt;, even if your hard drive dies or you forget your passphrase.  Without it, you could permanently lose your coins!  The backup buttons are to the right of the address list.&lt;br&gt;&lt;br&gt;A paper backup is recommended, and it can be copied by hand if you do not have a working printer. A digital backup only works if you remember the passphrase used at the time it was created.  If you have ever forgotten a password before, only rely on a digital backup if you store the password with it!&lt;br&gt;&lt;br&gt;&lt;a href=&quot;https://bitcointalk.org/index.php?topic=152151.0&quot;&gt;Read more about Armory backups&lt;/a&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1744"/>
        <source>&lt;font color=&quot;%1&quot;&gt;&lt;b&gt;Backup This Wallet&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1763"/>
        <source>&lt;b&gt;&lt;font color=&quot;%1&quot;&gt;Maximum Funds:&lt;/font&gt;&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1764"/>
        <source>&lt;b&gt;Spendable Funds:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1765"/>
        <source>&lt;b&gt;Unconfirmed:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1812"/>
        <source>Copy Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1813"/>
        <source>Display Address QR Code</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1814"/>
        <source>View Address on %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1815"/>
        <source>Request Payment to this Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1816"/>
        <source>Copy Hash160 (hex)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1817"/>
        <source>Copy Raw Public Key (hex)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1818"/>
        <source>Copy Comment</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1819"/>
        <source>Copy Balance</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1844"/>
        <source>Could not open browser</source>
        <translation>Konnte Browser nicht öffnen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1844"/>
        <source>Armory encountered an error opening your web browser.  To viewthis address on blockchain.info, please copy and pastethe following URL into your browser:&lt;br&gt;&lt;br&gt;&lt;a href=&quot;%1&quot;&gt;%2&lt;/a&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1940"/>
        <source>Invalid Passphrase</source>
        <translation>Ungültiges Passwort</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1940"/>
        <source>Previous passphrase is not correct!  Could not unlock wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1957"/>
        <source>Changing Encryption</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1950"/>
        <source>No Encryption</source>
        <translation>Keine Verschlüsselung</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1961"/>
        <source>Encrypted (AES256)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1975"/>
        <source>Offline Mode</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1975"/>
        <source>Armory is currently running in offline mode, and has noability to determine balances or create transactions.&lt;br&gt;&lt;br&gt;In order to send coins from this wallet you must use afull copy of this wallet from an online computer,or initiate an &quot;offline transaction&quot; using a watching-onlywallet on an online computer.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1985"/>
        <source>Armory Not Ready</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="1985"/>
        <source>
           Armory is currently scanning the blockchain to collect
           the information needed to create transactions.  This
           typically takes between one and five minutes.  Please
           wait until your balance appears on the main window,
           then try again.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2016"/>
        <source>Create Paper Backup</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2021"/>
        <source>Move along...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2021"/>
        <source>This wallet does not contain any private keys.  Nothing to backup!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2035"/>
        <source>Unlock Private Keys</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2044"/>
        <source>Unlock Failed</source>
        <translation>Entsperren fehlgeschlagen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2038"/>
        <source>
                  Wallet was not be unlocked.  The public keys and addresses
                  will still be shown, but private keys will not be available
                  unless you reopen the dialog with the correct passphrase</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2044"/>
        <source>
                  Wallet could not be unlocked to display individual keys.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2055"/>
        <source>No Selection</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2055"/>
        <source>You must select an address to remove!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2072"/>
        <source>Invalid Selection</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2072"/>
        <source>
               You cannot delete addresses generated by your wallet.
               Only imported addresses can be deleted.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2081"/>
        <source>Imported Address Warning</source>
        <translation>Eingelesene Adresse Warnung</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2081"/>
        <source>Armory supports importing of external private keys into yourwallet but imported addresses are &lt;u&gt;not&lt;/u&gt; automaticallyprotected by your backups.  If you do not plan to use theaddress again, it is recommended that you &quot;Sweep&quot; the privatekey instead of importing it.&lt;br&gt;&lt;br&gt;Individual private keys, including imported ones, can bebacked up using the &quot;Export Key Lists&quot; option in the walletbackup window.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2113"/>
        <source>Move along... This wallet does not have
                             a chain code. Backups are pointless!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2138"/>
        <source>
            This is the name stored with the wallet file.  Click on the
            &quot;Change Labels&quot; button on the right side of this
            window to change this field</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2143"/>
        <source>
            This is the description of the wallet stored in the wallet file.
            Press the &quot;Change Labels&quot; button on the right side of this
            window to change this field</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2148"/>
        <source>
            This is a unique identifier for this wallet, based on the root key.
            No other wallet can have the same ID
            unless it is a copy of this one, regardless of whether
            the name and description match.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2154"/>
        <source>
            This is the number of addresses *used* by this wallet so far.
            If you recently restored this wallet and you do not see all the
            funds you were expecting, click on this field to increase it.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2160"/>
        <source>
            Offline:  This is a &quot;Watching-Only&quot; wallet that you have identified
            belongs to you, but you cannot spend any of the wallet funds
            using this wallet.  This kind of wallet
            is usually stored on an internet-connected computer, to manage
            incoming transactions, but the private keys needed
            to spend the money are stored on an offline computer.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2168"/>
        <source>
            Watching-Only:  You can only watch addresses in this wallet
            but cannot spend any of the funds.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2172"/>
        <source>
            No Encryption: This wallet contains private keys, and does not require
            a passphrase to spend funds available to this wallet.  If someone
            else obtains a copy of this wallet, they can also spend your funds!
            (You can click the &quot;Change Encryption&quot; button on the right side of this
            window to enabled encryption)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2179"/>
        <source>
            This wallet contains the private keys needed to spend this wallet's
            funds, but they are encrypted on your harddrive.  The wallet must be
            &quot;unlocked&quot; with the correct passphrase before you can spend any of the
            funds.  You can still generate new addresses and monitor incoming
            transactions, even with a locked wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2186"/>
        <source>
            Declare who owns this wallet.  If you click on the field and select
            &quot;This wallet is mine&quot;, it's balance will be included in your total
            Armory Balance in the main window</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2191"/>
        <source>
            This is exactly how long it takes your computer to unlock your
            wallet after you have entered your passphrase.  If someone got
            ahold of your wallet, this is approximately how long it would take
            them to for each guess of your passphrase.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2197"/>
        <source>
            This is the amount of memory required to unlock your wallet.
            Memory values above 64 kB pretty much guarantee that GPU-acceleration
            will be useless for guessing your passphrase</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2202"/>
        <source>
            Wallets created with different versions of Armory, may have
            different wallet versions.  Not all functionality may be
            available with all wallet versions.  Creating a new wallet will
            always create the latest version.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2208"/>
        <source>Wallet Name:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2209"/>
        <source>Description:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2211"/>
        <source>Wallet ID:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2212"/>
        <source>Addresses Used:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2213"/>
        <source>Security:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2214"/>
        <source>Version:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2216"/>
        <source>Belongs to:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2222"/>
        <source>Unlock Time:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2223"/>
        <source>Unlock Memory:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2371"/>
        <source>You own this wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2249"/>
        <source>Someone else...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2259"/>
        <source>Click to Test</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2373"/>
        <source>&lt;i&gt;Offline&lt;/i&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2382"/>
        <source>Someone else</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2383"/>
        <source>&lt;i&gt;Watching-Only&lt;/i&gt;</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgWalletSelect</name>
    <message>
        <location filename="qtdialogs.py" line="4747"/>
        <source>No Wallets!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4747"/>
        <source>There are no wallets to select from.  Please create or import
            a wallet first.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="4776"/>
        <source>Select Wallet</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>DlgWltRecoverWallet</name>
    <message>
        <location filename="qtdialogs.py" line="13857"/>
        <source>Browse File System</source>
        <translation>Dateisystem durchsuchen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13861"/>
        <source>
         &lt;b&gt;Wallet Recovery Tool:
         &lt;/b&gt;&lt;br&gt;
         This tool will recover data from damaged or inconsistent
         wallets.  Specify a wallet file and Armory will analyze the
         wallet and fix any errors with it.
         &lt;br&gt;&lt;br&gt;
         &lt;font color=&quot;%1&quot;&gt;If any problems are found with the specified
         wallet, Armory will provide explanation and instructions to
         transition to a new wallet. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13873"/>
        <source>Wallet Path:</source>
        <translation>Wallet Pfad:</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13878"/>
        <source>Select Wallet...</source>
        <translation>Wähle Wallet...</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13884"/>
        <source>Select Loaded Wallet</source>
        <translation>Geladene Wallet auswählen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13908"/>
        <source>&lt;b&gt;Stripped Recovery&lt;/b&gt;&lt;br&gt;Only attempts to                             recover the wallet&apos;s rootkey and chaincode</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13915"/>
        <source>&lt;b&gt;Bare Recovery&lt;/b&gt;&lt;br&gt;Attempts to recover all private key related data</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13922"/>
        <source>&lt;b&gt;Full Recovery&lt;/b&gt;&lt;br&gt;Attempts to recover as much data as possible</source>
        <translation>&lt;b&gt;Vollständige Wiederherstellung&lt;/b&gt;&lt;br&gt;Versucht so viele Daten wie möglich wiederherzustellen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13928"/>
        <source>&lt;b&gt;Consistency Check&lt;/b&gt;&lt;br&gt;Checks wallet consistency. Works with both full and watch only&lt;br&gt; wallets. Unlocking of encrypted wallets is not mandatory</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13972"/>
        <source>Recover</source>
        <translation>Wiederherstellen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13973"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13984"/>
        <source>The entered path does not exist</source>
        <translation>Der eingegebene Pfad existiert nicht</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13997"/>
        <source>Wallet Recovery Tool</source>
        <translation>Wallet Wiederherstellungswerkzeug</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14041"/>
        <source>Wallet files (*.wallet);; All files (*)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="14048"/>
        <source>Recover Wallet</source>
        <translation>Wallets wiederherstellen</translation>
    </message>
</context>
<context>
    <name>FeeSelectionDialog</name>
    <message>
        <location filename="FeeSelectUI.py" line="53"/>
        <source>Flat Fee (BTC)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="FeeSelectUI.py" line="75"/>
        <source>Fee/Byte (Satoshi/Byte)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="FeeSelectUI.py" line="97"/>
        <source>Auto Fee/Byte (Satoshi/Byte)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="FeeSelectUI.py" line="105"/>
        <source>Fetch fee/byte from your network node</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="FeeSelectUI.py" line="107"/>
        <source>Failed to fetch fee/byte from node</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="FeeSelectUI.py" line="121"/>
        <source>Close</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="FeeSelectUI.py" line="124"/>
        <source>Adjust fee/byte for privacy</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="FeeSelectUI.py" line="145"/>
        <source>Select Fee Type</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>LedgerDispModelSimple</name>
    <message>
        <location filename="armorymodels.py" line="254"/>
        <source>Transaction confirmed!
(%d confirmations)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="259"/>
        <source>%d/120 confirmations</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="260"/>
        <source>

This is a &quot;generation&quot; transaction from
Bitcoin mining.  These transactions take
120 confirmations (approximately one day)
before they are available to be spent.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="265"/>
        <source>This is a mempool replaceable transaction. Do not consider you have been sent these coins until this transaction has at least 1 confirmation.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="269"/>
        <source>%d/6 confirmations</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="270"/>
        <source>

For small transactions, 2 or 3 confirmations is usually acceptable. For larger transactions, you should wait for 6 confirmations before trusting that the transaction is valid.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="280"/>
        <source>Bitcoins sent and received by the same wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="285"/>
        <source>You mined these Bitcoins!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="287"/>
        <source>Bitcoins sent</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="289"/>
        <source>Bitcoins received</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="292"/>
        <source>The net effect on the balance of this wallet &lt;b&gt;not including transaction fees.&lt;/b&gt;  You can change this behavior in the Armory preferences window.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="297"/>
        <source>The net effect on the balance of this wallet, including transaction fees.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="307"/>
        <source>Date</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="308"/>
        <source>Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="308"/>
        <source>Wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="309"/>
        <source>Comments</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="311"/>
        <source>Amount</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="312"/>
        <source>Other Owner</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="313"/>
        <source>Wallet ID</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="314"/>
        <source>Tx Hash (LE)</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>LetterButton</name>
    <message>
        <location filename="qtdialogs.py" line="349"/>
        <source>SPACE</source>
        <translation>SPACE</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="354"/>
        <source>SHIFT</source>
        <translation>SHIFT</translation>
    </message>
    <message>
        <location filename="qtdialogs.py" line="358"/>
        <source>DEL</source>
        <translation>DEL</translation>
    </message>
</context>
<context>
    <name>LockboxDisplayModel</name>
    <message>
        <location filename="MultiSigModels.py" line="90"/>
        <source>Scanning: %1%%</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>LockboxSelectFrame</name>
    <message>
        <location filename="WalletFrames.py" line="28"/>
        <source>Invalid Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="28"/>
        <source> There was 
         an error loading the specified lockbox (%1).</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="34"/>
        <source> &lt;font color=&quot;%1&quot; size=4&gt;&lt;b&gt;&lt;u&gt;Lockbox
         %2 (%3-of-%4)&lt;/u&gt;&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="39"/>
        <source>Lockbox ID:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="40"/>
        <source>Name:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="41"/>
        <source>Description:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="42"/>
        <source>Spendable BTC:</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>MessageSigningVerificationDialog</name>
    <message>
        <location filename="toolsDialogs.py" line="24"/>
        <source>Message Signing/Verification</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="32"/>
        <source>Sign Message</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="33"/>
        <source>Verify Bare Signature</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="34"/>
        <source>Verify Signed Message Block</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="37"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
</context>
<context>
    <name>MessageSigningWidget</name>
    <message>
        <location filename="toolsDialogs.py" line="63"/>
        <source>Sign with Address:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="72"/>
        <source>Message to sign:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="82"/>
        <source>Bare Signature (Bitcoin Core Compatible)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="83"/>
        <source>Base64 Signature</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="84"/>
        <source>Clearsign Signature</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="92"/>
        <source>Message Signature:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="99"/>
        <source>Copy Signature</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="100"/>
        <source>Clear All</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="125"/>
        <source>Unlock Wallet to Import</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="127"/>
        <source>Wallet is Locked</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="127"/>
        <source>Cannot import private keys without unlocking wallet!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="168"/>
        <source>Non ASCII Text</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="168"/>
        <source>Message to sign must be ASCII</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="179"/>
        <source>Private Key Not Known</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="179"/>
        <source>The private key is not known for this address.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="173"/>
        <source>Invalid Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="173"/>
        <source>The signing address is invalid.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>MirrorWalletsDialog</name>
    <message>
        <location filename="WalletMirrorDialog.py" line="119"/>
        <source>
      Starting v0.96, Armory needs to mirror Python
      wallets into C++ wallets in order to operate. Mirrored C++ wallets
      are watching only (they do not hold any private keys).&lt;br&gt;&lt;br&gt;
            
      Mirrored wallets are used to interface with the database and perform
      operations that aren't available to the legacy Python wallets, such
      support for compressed public keys and Segregated Witness transactions.
      &lt;br&gt;&lt;br&gt;
      
      Mirroring only needs to happen once per wallet. Synchronization
      will happen every time the Python wallet address chain is ahead of the 
      mirrored Cpp wallet address chain (this typically rarely happens).
      &lt;br&gt;&lt;br&gt;
      
      This process can take up to a few minutes per wallet.&lt;br&gt;&lt;br&gt;
      </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletMirrorDialog.py" line="154"/>
        <source>Mirroring Wallets</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>NewWalletFrame</name>
    <message>
        <location filename="WalletFrames.py" line="352"/>
        <source>Wallet &amp;name:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="359"/>
        <source>Wallet &amp;description:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="364"/>
        <source>Add Manual &amp;Entropy</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="381"/>
        <source>Configure</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="385"/>
        <source>Advanced Options</source>
        <translation>Erweiterte Optionen</translation>
    </message>
</context>
<context>
    <name>OutputBundle</name>
    <message>
        <location filename="MultiSigDialogs.py" line="2630"/>
        <source>Unrelated Multi-Spend</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2630"/>
        <source>
               The signature-collector you loaded appears to be
               unrelated to any of the wallets or lockboxes that you have
               available.  If you were expecting to be able to sign for a
               lockbox input, you need to import the lockbox definition    
               first.  Any other person or device with the lockbox loaded
               can export it to be imported by this device.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2638"/>
        <source>Cannot Sign</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2638"/>
        <source>
               The signature-collector you loaded is sending money to one
               of your wallets or lockboxes, but does not have any inputs
               for which you can sign.  
               If you were expecting to be able to sign for a
               lockbox input, you need to import the lockbox definition    
               first.  Any other person or device with the lockbox loaded
               can export it to be imported by this device.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2676"/>
        <source>
               &lt;b&gt;&lt;u&gt;Spending:&lt;/u&gt; &lt;font color=&quot;%1&quot;&gt;%2&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2681"/>
        <source>
               &lt;b&gt;&lt;u&gt;Contributor:&lt;/u&gt; &lt;font color=&quot;%1&quot;&gt;%2&lt;/b&gt;%3&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2757"/>
        <source>[[Unknown Signer]]</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2810"/>
        <source>
            &lt;b&gt;&lt;u&gt;Receiving:&lt;/u&gt;  &lt;font color=&quot;%1&quot;&gt;%2&lt;/font&gt;&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2858"/>
        <source>Import/Merge</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2861"/>
        <source>Broadcast</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2862"/>
        <source>Export</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2863"/>
        <source>Done</source>
        <translation>Fertig</translation>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2900"/>
        <source>Review and Sign</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2919"/>
        <source>Sign Lockbox</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2921"/>
        <source>Wallet is locked</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2921"/>
        <source>Cannot sign without unlocking wallet!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="2974"/>
        <source>Done!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3014"/>
        <source>
            from any online computer (you are currently offline)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3019"/>
        <source>
         &lt;font color=&quot;%1&quot;&gt;This transaction has enough signatures and
         can be broadcast %2&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3028"/>
        <source>
            &lt;font color=&quot;%1&quot;&gt;This transaction is incomplete.  You can
            add signatures then export and give to other parties or
            devices to sign.&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3043"/>
        <source>Export Signature Collector</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3044"/>
        <source>
         The text below includes all data about this multi-sig transaction, 
         including all the signatures already made to it.  It contains 
         everything needed to securely review and sign it, including offline 
         devices/wallets.  
         &lt;br&gt;&lt;br&gt;
         If this transaction requires signatures from multiple parties, it is
         safe to send this data via email or USB key.  No data is included 
         that would compromise the security of any of the signing devices.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3061"/>
        <source>Import Signature Collector</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3062"/>
        <source>
         Load a multi-sig transaction for review, signing and/or broadcast.  
         If any of your loaded wallets can sign for any transaction inputs,
         you will be able to execute the signing for each one.  If your 
         signature completes the transaction, you can then broadcast it to
         finalize it.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3093"/>
        <source>Invalid Signatures</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="3093"/>
        <source>
            Somehow not all inputs have valid sigantures!  You can choose  
            to attempt to broadcast anyway, in case you think Armory is
            not evaluating the transaction state correctly.  
            &lt;br&gt;&lt;br&gt;
            Otherwise, please confirm that you have created signatures 
            from the correct wallets.  Perhaps try collecting signatures
            again...?</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>PromissoryCollectModel</name>
    <message>
        <location filename="armorymodels.py" line="1493"/>
        <source>Note ID</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1493"/>
        <source>Label</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1493"/>
        <source>Funding</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1493"/>
        <source>Fee</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>ReviewOfflineTxFrame</name>
    <message>
        <location filename="TxFrames.py" line="1174"/>
        <source>There is no security-sensitive information in this data below, so it is perfectly safe to copy-and-paste it into an email message, or save it to a borrowed USB key.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1179"/>
        <source>Save as file...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1181"/>
        <source>Save this data to a USB key or other device, to be transferred to a computer that contains the private keys for this wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1185"/>
        <source>Copy to clipboard</source>
        <translation>In Zwischenspeicher kopieren</translation>
    </message>
    <message>
        <location filename="TxFrames.py" line="1190"/>
        <source>Copy the transaction data to the clipboard, so that it can be pasted into an email or a text document.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1194"/>
        <source>&lt;b&gt;Instructions for completing this transaction:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1252"/>
        <source>&lt;b&gt;Transaction Data&lt;/b&gt; <byte value="x9"/> (Unsigned ID: %1)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1259"/>
        <source>
            The block of data shown below is the complete transaction you 
            just requested, but is invalid because it does not contain any
            signatures.  You must take this data to the computer with the 
            full wallet to get it signed, then bring it back here to be
            broadcast to the Bitcoin network.
            &lt;br&gt;&lt;br&gt;
            Use &quot;Save as file...&quot; to save an &lt;i&gt;*.unsigned.tx&lt;/i&gt; 
            file to USB drive or other removable media.  
            On the offline computer, click &quot;Offline Transactions&quot; on the main 
            window.  Load the transaction, &lt;b&gt;review it&lt;/b&gt;, then sign it 
            (the filename now end with &lt;i&gt;*.signed.tx&lt;/i&gt;).  Click &quot;Continue&quot; 
            below when you have the signed transaction on this computer.  
            &lt;br&gt;&lt;br&gt;
            &lt;b&gt;NOTE:&lt;/b&gt; The USB drive only ever holds public transaction
            data that will be broadcast to the network.  This data may be 
            considered privacy-sensitive, but does &lt;u&gt;not&lt;/u&gt; compromise
            the security of your wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1278"/>
        <source>
            You have chosen to create the previous transaction but not sign 
            it or broadcast it, yet.  You can save the unsigned 
            transaction to file, or copy&amp;paste from the text box.  
            You can use the following window (after clicking &quot;Continue&quot;) to 
            sign and broadcast the transaction when you are ready</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>ReviewOfflineTxPage</name>
    <message>
        <location filename="Wizards.py" line="423"/>
        <source>Review Offline Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="425"/>
        <source>Step 2: Review Offline Transaction</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>SelectWalletFrame</name>
    <message>
        <location filename="WalletFrames.py" line="115"/>
        <source>No Wallets!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="115"/>
        <source>There are no wallets to select from.  Please create or import a wallet first.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="159"/>
        <source>Wallet ID:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="160"/>
        <source>Name:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="161"/>
        <source>Description:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="162"/>
        <source>Spendable BTC:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="302"/>
        <source>Source: All addresses</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="199"/>
        <source>Coin Control</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="256"/>
        <source>Source: None selected</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="260"/>
        <source>Source: %1...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="262"/>
        <source>Source: %1 Outputs</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="302"/>
        <source>Source: 0 addresses</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="322"/>
        <source>*Coin Control Subset*</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="329"/>
        <source>(available when online)</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>SendBitcoinsFrame</name>
    <message>
        <location filename="TxFrames.py" line="56"/>
        <source>Transaction fees go to users who contribute computing power to keep the Bitcoin network secure, and in return they get your transaction included in the blockchain faster.  &lt;b&gt;Most transactions do not require a fee&lt;/b&gt; but it is recommended anyway since it guarantees quick processing and helps the network.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="74"/>
        <source>Use an existing address for change</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="75"/>
        <source>Send change to first input address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="76"/>
        <source>Specify a change address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="77"/>
        <source>Change:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="86"/>
        <source>Remember for future transactions</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="89"/>
        <source>Most transactions end up with oversized inputs and Armory will send the change to the next address in this wallet.  You may change this behavior by checking this box.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="93"/>
        <source>Guarantees that no new addresses will be created to receive change. This reduces anonymity, but is useful if you created this wallet solely for managing imported addresses, and want to keep all funds within existing addresses.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="98"/>
        <source>You can specify any valid Bitcoin address for the change.  &lt;b&gt;NOTE:&lt;/b&gt; If the address you specify is not in this wallet, Armory will not be able to distinguish the outputs when it shows up in your ledger.  The change will look like a second recipient, and the total debit to your wallet will be equal to the amount you sent to the recipient &lt;b&gt;plus&lt;/b&gt; the change.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="105"/>
        <source>Check this box to create an unsigned transaction to be signed and/or broadcast later.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="108"/>
        <source>Create Unsigned</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="282"/>
        <source>Send!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="110"/>
        <source>Cancel</source>
        <translation>Abbrechen</translation>
    </message>
    <message>
        <location filename="TxFrames.py" line="152"/>
        <source>Manually Enter &quot;bitcoin:&quot; Link</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="153"/>
        <source>
         Armory does not always succeed at registering itself to handle 
         URL links from webpages and email.  
         Click this button to copy a &quot;bitcoin:&quot; link directly into Armory.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="218"/>
        <source>&lt;b&gt;Sending from Wallet:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="233"/>
        <source>Send Bitcoins</source>
        <translation>Bitcoins versenden</translation>
    </message>
    <message>
        <location filename="TxFrames.py" line="279"/>
        <source>Continue</source>
        <translation>Fortfahren</translation>
    </message>
    <message>
        <location filename="TxFrames.py" line="280"/>
        <source>Click to create an unsigned transaction!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="283"/>
        <source>Click to send bitcoins!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="898"/>
        <source>Invalid Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="505"/>
        <source>You have entered an invalid address. The error has been highlighted on the entrry screen.</source>
        <comment>You have entered %1 invalid addresses. The errors have been highlighted on the entry screen</comment>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="516"/>
        <source>Wrong Network!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="516"/>
        <source>
                     Address %1 is for the wrong network!  You are on the &lt;b&gt;%2&lt;/b&gt;
                     and the address you supplied is for the the &lt;b&gt;%3&lt;/b&gt;!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="533"/>
        <source>Zero Amount</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="533"/>
        <source>You cannot send 0 BTC to any recipients.  &lt;br&gt;Please enter a positive amount for recipient %1.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="539"/>
        <source>Negative Value</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="539"/>
        <source>You have specified a negative amount for recipient %1. &lt;br&gt;Only positive values are allowed!.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="544"/>
        <source>Too much precision</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="544"/>
        <source>Bitcoins can only be specified down to 8 decimal places. The smallest value that can be sent is  0.0000 0001 BTC. Please enter a new amount for recipient %1.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="550"/>
        <source>Missing recipient amount</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="550"/>
        <source>You did not specify an amount to send!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="554"/>
        <source>Invalid Value String</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="554"/>
        <source>The amount you specified to send to address %1 is invalid (%2).</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="576"/>
        <source>Non-Standard to Spend</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="576"/>
        <source>
                  Due to the Lockbox size (%1-of-%2) of recipient %3, spending
                  funds from this Lockbox is valid but non-standard for versions
                  of Bitcoin prior to 0.10.0. This means if your version of
                  Bitcoin is 0.9.x or below, and you try to broadcast a
                  transaction that spends from this Lockbox the transaction
                  will not be accepted. If you have version 0.10.0, but all
                  of your peers have an older version your transaction will
                  not be forwarded to the rest of the network. If you deposit
                  Bitcoins into this Lockbox you may have to wait until you
                  and at least some of your peers have upgraded to 0.10.0
                  before those Bitcoins can be spent. Alternatively, if you
                  have enough computing power to mine your own transactions,
                  or know someone who does, you can arrange to have any valid
                  but non-standard transaction included in the block chain.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="603"/>
        <source>Excessive Fee</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="603"/>
        <source>
            Your specified fee results in a rate of &lt;b&gt;%1 satoshis per byte/b&gt;. 
            This is much higher than the median satoshi/byte rate of &lt;b&gt;%2 BTC&lt;/b&gt;.
            Are you &lt;i&gt;absolutely sure&lt;/i&gt; that you want to send with this
            fee?  
            &lt;br&gt;&lt;br&gt;
            If you do not want this fee, click &quot;No&quot; and then change the fee
            at the bottom of the &quot;Send Bitcoins&quot; window before trying 
            again.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="617"/>
        <source>Insufficient Fee</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="617"/>
        <source>
            Your specified fee results in a rate of &lt;b&gt;%d satoshis per byte/b&gt;. 
            This is much lower than the median satoshi/byte rate of &lt;b&gt;%s BTC&lt;/b&gt;.
            Are you &lt;i&gt;absolutely sure&lt;/i&gt; that you want to send with this
            fee?  
            &lt;br&gt;&lt;br&gt;
            If you do not want this fee, click &quot;No&quot; and then change the fee
            at the bottom of the &quot;Send Bitcoins&quot; window before trying 
            again.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="632"/>
        <source>Coin Selection Error</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="632"/>
        <source>
            There was an error constructing your transaction, due to a 
            quirk in the way Bitcoin transactions work.  If you see this
            error more than once, try sending your BTC in two or more 
            separate transactions.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="754"/>
        <source>Wallet is Locked</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="754"/>
        <source>Cannot sign transaction while your wallet is locked. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="898"/>
        <source>
                     You specified an invalid change address for this 
                     transcation.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="941"/>
        <source>Invalid Input</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="941"/>
        <source>Cannot compute the maximum amount because there is an error in the amount for recipient %1.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="950"/>
        <source>Insufficient funds</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="950"/>
        <source>You have specified more than your spendable balance to the other recipients and the transaction fee.  Therefore, the maximum amount for this recipient would actually be negative.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="965"/>
        <source>&lt;u&gt;&lt;/u&gt;Fills in the maximum spendable amount minus the amounts specified for other recipients and the transaction fee </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1074"/>
        <source>+ Recipient</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1076"/>
        <source>- Recipient</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>SentToAddrBookModel</name>
    <message>
        <location filename="armorymodels.py" line="1408"/>
        <source>Address</source>
        <translation>Adressen</translation>
    </message>
    <message>
        <location filename="armorymodels.py" line="1409"/>
        <source>Ownership</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1410"/>
        <source>Times Used</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1411"/>
        <source>Comment</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>SetPassphraseFrame</name>
    <message>
        <location filename="WalletFrames.py" line="562"/>
        <source>Please enter a passphrase for wallet encryption.

A good passphrase consists of at least 10 or more
random letters, or 6 or more random words.
</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="567"/>
        <source>New Passphrase:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="572"/>
        <source>Again:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="607"/>
        <source>&lt;font color=%1&gt;&lt;b&gt;Passphrase is non-ASCII!&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="611"/>
        <source>&lt;font color=%1&gt;&lt;b&gt;Passphrases do not match!&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="615"/>
        <source>&lt;font color=%1&gt;&lt;b&gt;Passphrase is too short!&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="619"/>
        <source>&lt;font color=%1&gt;&lt;b&gt;Passphrases match!&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>SignBroadcastOfflineTxFrame</name>
    <message>
        <location filename="TxFrames.py" line="1339"/>
        <source>Sign</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1340"/>
        <source>Broadcast</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1341"/>
        <source>Save file...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1342"/>
        <source>Load file...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1343"/>
        <source>Copy Text</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1344"/>
        <source>Copy Raw Tx (Hex)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1363"/>
        <source>Signature is Invalid!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1374"/>
        <source>This is wallet from which the offline transaction spends bitcoins</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1381"/>
        <source>The name of the wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1382"/>
        <source>&lt;b&gt;Wallet Label:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1392"/>
        <source>&lt;b&gt;Pre-Broadcast ID:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1397"/>
        <source>Net effect on this wallet&apos;s balance</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1399"/>
        <source>&lt;b&gt;Transaction Amount:&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1402"/>
        <source>Click here for more&lt;br&gt; information about &lt;br&gt;this transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1473"/>
        <source>Inconsistent Data!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1473"/>
        <source>This transaction contains inconsistent information.  This is probably not your fault...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1479"/>
        <source>Wrong Network!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1479"/>
        <source>This transaction is actually for a different network!  Did you load the correct transaction?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1495"/>
        <source>No connection to Bitcoin network!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1506"/>
        <source>&lt;b&gt;&lt;font color=&quot;red&quot;&gt;Unrecognized!&lt;/font&gt;&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1516"/>
        <source>Offline Warning</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1516"/>
        <source>&lt;b&gt;Please review your transaction carefully before signing and broadcasting it!&lt;/b&gt;  The extra security of using offline wallets is lost if you do not confirm the transaction is correct!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1522"/>
        <source>&lt;b&gt;&lt;font color=&quot;red&quot;&gt;Unsigned&lt;/font&gt;&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1526"/>
        <source>&lt;b&gt;&lt;font color=&quot;red&quot;&gt;Bad Signature!&lt;/font&gt;&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1530"/>
        <source>&lt;b&gt;&lt;font color=&quot;green&quot;&gt;All Signatures Valid!&lt;/font&gt;&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1549"/>
        <source>Multiple Input Wallets</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1549"/>
        <source>Somehow, you have obtained a transaction that actually pulls from more than one wallet.  The support for handling multi-wallet signatures is not currently implemented (this also could have happened if you imported the same private key into two different wallets).</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1557"/>
        <source>Unrelated Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1557"/>
        <source>This transaction appears to have no relationship to any of the wallets stored on this computer.  Did you load the correct transaction?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1617"/>
        <source>[[ Unrelated ]]</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1637"/>
        <source>Invalid Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1637"/>
        <source>Transaction data is invalid and cannot be shown!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1650"/>
        <source>Cannot Sign</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1650"/>
        <source>This transaction is not relevant to any of your wallets.Did you load the correct transaction?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1656"/>
        <source>Not Signable</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1656"/>
        <source>This is not a valid transaction, and thus it cannot be signed. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1661"/>
        <source>Already Signed</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1661"/>
        <source>This transaction has already been signed!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1667"/>
        <source>No Private Keys!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1667"/>
        <source>This transaction refers one of your wallets, but that wallet is a watching-only wallet.  Therefore, private keys are not available to sign this transaction.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1690"/>
        <source>Missing Change</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1690"/>
        <source>
            This transaction has %1 recipients, and none of them
            are addresses in this wallet (for receiving change).  
            This can happen if you specified a custom change address 
            for this transaction, or sometimes happens solely by 
            chance with a multi-recipient transaction.  It could also 
            be the result of someone tampering with the transaction. 
            &lt;br&gt;&lt;br&gt;The transaction is valid and ready to be signed.  
            Please verify the recipient and amounts carefully before 
            confirming the transaction on the next screen.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1710"/>
        <source>Send Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1717"/>
        <source>Wallet is Locked</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1717"/>
        <source>Cannot sign transaction while your wallet is locked. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1743"/>
        <source>No Internet!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1736"/>
        <source>Armory lost its connection to , and cannot broadcast any transactions until it is reconnected. Please verify that  (or bitcoind) is open and synchronized with the network.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1743"/>
        <source>You do not currently have a connection to the Bitcoin network. If this does not seem correct, verify that  is open and synchronized with the network.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1754"/>
        <source>Signature Error</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1754"/>
        <source>
            Not all signatures are valid.  This transaction
            cannot be broadcast.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1758"/>
        <source>Error</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1758"/>
        <source>
            There was an error processing this transaction, for reasons 
            that are probably not your fault...</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1784"/>
        <source>File Remove Error</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1784"/>
        <source>The file could not be deleted.  If you want to delete it, please do so manually.  The file was loaded from: &lt;br&gt;&lt;br&gt;%1: </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1822"/>
        <source>Formatting Error</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1822"/>
        <source>The transaction data was not in a format recognized by Armory.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1814"/>
        <source>Transaction Saved!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1814"/>
        <source>Your transaction has been saved to the following location:

%1

It can now be broadcast from any computer running Armory in online mode.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1853"/>
        <source>Load Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TxFrames.py" line="1875"/>
        <source>&lt;i&gt;Copied!&lt;/i&gt;</source>
        <translation>&lt;i&gt;Kopiert!&lt;/i&gt;</translation>
    </message>
</context>
<context>
    <name>SignBroadcastOfflineTxPage</name>
    <message>
        <location filename="Wizards.py" line="430"/>
        <source>Sign/Broadcast Offline Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="432"/>
        <source>Step 3: Sign/Broadcast Offline Transaction</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>SignatureVerificationWidget</name>
    <message>
        <location filename="toolsDialogs.py" line="200"/>
        <source>Verify Signature</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="201"/>
        <source>Clear All</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="222"/>
        <source>
      The owner of the following Bitcoin address...
      &lt;br&gt;
      &lt;blockquote&gt;
      &lt;font face=&quot;Courier&quot; size=4 color=&quot;#000060&quot;&gt;&lt;b&gt;%1&lt;/b&gt;&lt;/font&gt;
      &lt;/blockquote&gt;
      &lt;br&gt;
      ... has produced a &lt;b&gt;&lt;u&gt;valid&lt;/u&gt;&lt;/b&gt; signature for
      the following message:&lt;br&gt;
      </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="239"/>
        <source>Verified!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="239"/>
        <source>
            %`
            &lt;hr&gt;
            &lt;blockquote&gt;
            &lt;font face=&quot;Courier&quot; color=&quot;#000060&quot;&gt;&lt;b&gt;%1&lt;/b&gt;&lt;/font&gt;
            &lt;/blockquote&gt;
            &lt;hr&gt;&lt;br&gt;
            &lt;b&gt;Please&lt;/b&gt; make sure that the address above (%2...) matches the
            exact address you were expecting.  A valid signature is meaningless 
            unless it is made
            from a recognized address!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="256"/>
        <source>Invalid Signature!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="256"/>
        <source>The supplied signature &lt;b&gt;is not valid&lt;/b&gt;!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="258"/>
        <source>&lt;font color=&quot;red&quot;&gt;Invalid Signature!&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>SignedMessageBlockVerificationWidget</name>
    <message>
        <location filename="toolsDialogs.py" line="312"/>
        <source>Signed Message Block:</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="toolsDialogs.py" line="320"/>
        <source>Message:</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>TreeStructure_CoinControl</name>
    <message>
        <location filename="TreeViewGUI.py" line="460"/>
        <source>Unspent Outputs</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TreeViewGUI.py" line="461"/>
        <source>RBF Eligible</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TreeViewGUI.py" line="462"/>
        <source>CPFP Outputs</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>TxInDispModel</name>
    <message>
        <location filename="armorymodels.py" line="1216"/>
        <source>Wallet ID</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1217"/>
        <source>Sender</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1218"/>
        <source>Amount</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1219"/>
        <source>Prev. Tx Hash</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1220"/>
        <source>Index</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1221"/>
        <source>From Block#</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1222"/>
        <source>Script Type</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1223"/>
        <source>Sequence</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1224"/>
        <source>Script</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>TxOutDispModel</name>
    <message>
        <location filename="armorymodels.py" line="1310"/>
        <source>Wallet ID</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1311"/>
        <source>Recipient</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1312"/>
        <source>Amount</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1313"/>
        <source>Script Type</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>TxWizard</name>
    <message>
        <location filename="Wizards.py" line="337"/>
        <source>Offline Transaction Wizard</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="381"/>
        <source>Create Unsigned Transaction</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="355"/>
        <source>Send!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="369"/>
        <source>Next</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>VerifyPassphraseFrame</name>
    <message>
        <location filename="WalletFrames.py" line="634"/>
        <source>&lt;font color=&quot;red&quot;&gt;&lt;b&gt;!!! DO NOT FORGET YOUR PASSPHRASE !!!&lt;/b&gt;&lt;/font&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="637"/>
        <source>&lt;b&gt;No one can help you recover you bitcoins if you forget the passphrase and don&apos;t have a paper backup!&lt;/b&gt; Your wallet and any &lt;u&gt;digital&lt;/u&gt; backups are useless if you forget it.  &lt;br&gt;&lt;br&gt;A &lt;u&gt;paper&lt;/u&gt; backup protects your wallet forever, against hard-drive loss and losing your passphrase.  It also protects you from theft, if the wallet was encrypted and the paper backup was not stolen with it.  Please make a paper backup and keep it in a safe place.&lt;br&gt;&lt;br&gt;Please enter your passphrase a third time to indicate that you are aware of the risks of losing your passphrase!&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>VerifyPassphrasePage</name>
    <message>
        <location filename="Wizards.py" line="297"/>
        <source>Invalid Passphrase</source>
        <translation>Ungültiges Passwort</translation>
    </message>
    <message>
        <location filename="Wizards.py" line="297"/>
        <source>You entered your confirmation passphrase incorrectly!</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>WalletAddrDispModel</name>
    <message>
        <location filename="armorymodels.py" line="1031"/>
        <source>&lt;u&gt;&lt;/u&gt;This is an imported address. Imported 
                               addresses are not protected by regular paper 
                               backups.  You must use the &quot;Backup Individual 
                               Keys&quot; option to protect it.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1036"/>
        <source>&lt;u&gt;&lt;/u&gt;The order that this address was 
                               generated in this wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1040"/>
        <source>This address was created by Armory to 
                            receive change-back-to-self from an oversized 
                            transaction.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1061"/>
        <source>Address</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1062"/>
        <source>Comment</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1063"/>
        <source>#Tx</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="1064"/>
        <source>Balance</source>
        <translation>Guthaben</translation>
    </message>
</context>
<context>
    <name>WalletBackupFrame</name>
    <message>
        <location filename="WalletFrames.py" line="679"/>
        <source>&lt;b&gt;Backup Options&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="680"/>
        <source>
         Armory wallets only need to be backed up &lt;u&gt;one time, ever.&lt;/u&gt;
         The backup is good no matter how many addresses you use. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="686"/>
        <source>Printable Paper Backup</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="688"/>
        <source>Single-Sheet (Recommended)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="690"/>
        <source>Fragmented Backup (M-of-N)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="693"/>
        <source>Digital Backup</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="695"/>
        <source>Unencrypted</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="697"/>
        <source>Encrypted</source>
        <translation>Verschlüsselt</translation>
    </message>
    <message>
        <location filename="WalletFrames.py" line="1035"/>
        <source>Export Key Lists</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="767"/>
        <source>
         Every time you click &quot;Receive Bitcoins,&quot; a new address is generated.
         All of these addresses are generated from a single seed value, which
         is included in all backups.   Therefore, all addresses that you have
         generated so far &lt;b&gt;and&lt;/b&gt; will ever be generated with this wallet, 
         are protected by this backup! </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="774"/>
        <source>
         &lt;i&gt;This wallet &lt;u&gt;does not&lt;/u&gt; currently have any imported
         addresses, so you can safely ignore this feature!&lt;/i&gt;
         When imported addresses are present, backups only protects those
         imported before the backup was made.  You must replace that
         backup if you import more addresses! </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="781"/>
        <source>
         Lost/forgotten passphrases are, &lt;b&gt;by far&lt;/b&gt;, the most common
         reason for users losing bitcoins.  It is critical you have
         at least one backup that works if you forget your wallet
         passphrase. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="787"/>
        <source>
         USB drives and CD/DVD disks are not intended for long-term storage.
         They will &lt;i&gt;probably&lt;/i&gt; last many years, but not guaranteed
         even for 3-5 years.   On the other hand, printed text on paper will
         last many decades, and useful even when thoroughly faded. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="793"/>
        <source>
         The ability to look at a backup and determine if
         it is still usable.   If a digital backup is stored in a safe
         deposit box, you have no way to verify its integrity unless
         you take a secure computer/device with you.  A simple glance at
         a paper backup is enough to verify that it is still intact. </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="800"/>
        <source>
         If multiple pieces/fragments are required to restore this wallet.
         For instance, encrypted backups require the backup
         &lt;b&gt;and&lt;/b&gt; the passphrase.  This feature is only needed for those
         concerned about physical security, not just online security.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="808"/>
        <source>Protects All Future Addresses</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="809"/>
        <source>Protects Imported Addresses</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="810"/>
        <source>Forgotten Passphrase</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="811"/>
        <source>Long-term Durability</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="812"/>
        <source>Visual Integrity</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="813"/>
        <source>Multi-Point Protection</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="841"/>
        <source>Create Backup</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="876"/>
        <source>
            When imported addresses are present, backups only protects those
            imported before the backup was made!  You must replace that
            backup if you import more addresses!
            &lt;i&gt;Your wallet &lt;u&gt;does&lt;/u&gt; contain imported addresses&lt;i&gt;.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="884"/>
        <source>
         &lt;b&gt;Backup Options for Wallet &quot;%1&quot; (%2)&lt;/b&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="897"/>
        <source>
               Paper backups protect every address ever generated by your
               wallet. It is unencrypted, which means it needs to be stored
               in a secure place, but it will help you recover your wallet
               if you forget your encryption passphrase!
               &lt;br&gt;&lt;br&gt;
               &lt;b&gt;You don't need a printer to make a paper backup!
               The data can be copied by hand with pen and paper.&lt;/b&gt;
               Paper backups are preferred to digital backups, because you
               know the paper backup will work no matter how many years (or
               decades) it sits in storage.  </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="908"/>
        <source>
               Digital backups can be saved to an external hard-drive or
               USB removable media.  It is recommended you make a few
               copies to protect against &quot;bit rot&quot; (degradation). &lt;br&gt;&lt;br&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="912"/>
        <source>
               &lt;b&gt;&lt;u&gt;IMPORTANT:&lt;/u&gt; Do not save an unencrypted digital
               backup to your primary hard drive!&lt;/b&gt;
               Please save it &lt;i&gt;directly&lt;/i&gt; to the backup device.
               Deleting the file does not guarantee the data is actually
               gone!  </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="918"/>
        <source>
               &lt;b&gt;&lt;u&gt;IMPORTANT:&lt;/u&gt; It is critical that you have at least
               one unencrypted backup!&lt;/b&gt;  Without it, your bitcoins will
               be lost forever if you forget your passphrase!  This is &lt;b&gt;
               by far&lt;/b&gt; the most common reason users lose coins!  Having
               at least one paper backup is recommended.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="924"/>
        <source>
               View and export invidivual addresses strings,
               public keys and/or private keys contained in your wallet.
               This is useful for exporting your private keys to be imported into
               another wallet app or service.
               &lt;br&gt;&lt;br&gt;
               You can view/backup imported keys, as well as unused keys in your
               keypool (pregenerated addresses protected by your backup that
               have not yet been used). </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="938"/>
        <source>Single-Sheet Paper Backup</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="947"/>
        <source>Fragmented Paper Backup</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="956"/>
        <source>Unencrypted Digital Backup</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="965"/>
        <source>Encrypted Digital Backup</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="1014"/>
        <source>Create Paper Backup</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="1023"/>
        <source>Create Digital Backup</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="1046"/>
        <source>Unlocking Wallet</source>
        <translation>Entsperre Wallet</translation>
    </message>
    <message>
        <location filename="WalletFrames.py" line="1073"/>
        <source>Unlock Failed</source>
        <translation>Entsperren fehlgeschlagen</translation>
    </message>
    <message>
        <location filename="WalletFrames.py" line="1067"/>
        <source>
                     Wallet was not be unlocked.  The public keys and addresses
                     will still be shown, but private keys will not be available
                     unless you reopen the dialog with the correct passphrase.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletFrames.py" line="1073"/>
        <source>
                     &apos;Wallet could not be unlocked to display individual keys.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>WalletComparisonClass</name>
    <message>
        <location filename="WalletMirrorDialog.py" line="73"/>
        <source>Mirroring wallet %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletMirrorDialog.py" line="84"/>
        <source>Synchronizing wallet %1</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="WalletMirrorDialog.py" line="91"/>
        <source>Checking imports for wallet %s</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>WalletCreationPage</name>
    <message>
        <location filename="Wizards.py" line="247"/>
        <source>Invalid Target Compute Time</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="247"/>
        <source>You entered Target Compute Time incorrectly.

Enter: &lt;Number&gt; (ms, s)</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="251"/>
        <source>Invalid Max Memory Usage</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="251"/>
        <source>You entered Max Memory Usag incorrectly.

nter: &lt;Number&gt; (kb, mb)</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>WalletWizard</name>
    <message>
        <location filename="Wizards.py" line="80"/>
        <source>Wallet Creation Wizard</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="148"/>
        <source>Wallet Backup Warning</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="148"/>
        <source>&lt;qt&gt;
               You have not made a backup for your new wallet.  You only have 
               to make a backup of your wallet &lt;u&gt;one time&lt;/u&gt; to protect 
               all the funds held by this wallet &lt;i&gt;any time in the future&lt;/i&gt;
               (it is a backup of the signing keys, not the coins themselves).
               &lt;br&gt;&lt;br&gt;
               If you do not make a backup, you will &lt;u&gt;permanently&lt;/u&gt; lose
               the money in this wallet if you ever forget your password, or 
               suffer from hardware failure.
               &lt;br&gt;&lt;br&gt;
               Are you sure that you want to leave this wizard without backing 
               up your wallet?&lt;/qt&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="186"/>
        <source>Creating Wallet</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>WizardCreateWatchingOnlyWalletFrame</name>
    <message>
        <location filename="WalletFrames.py" line="1091"/>
        <source>
               Your wallet has been created and is ready to be used.  It will
               appear in the &quot;&lt;i&gt;Available Wallets&lt;/i&gt;&quot; list in the main window.  
               You may click &quot;&lt;i&gt;Finish&lt;/i&gt;&quot; if you do not plan to use this 
               wallet on any other computer.
               &lt;br&gt;&lt;br&gt;
               A &lt;b&gt;watching-only wallet&lt;/b&gt; behaves exactly like a a regular 
               wallet, but does not contain any signing keys.  You can generate 
               addresses and confirm receipt of payments, but not spend or move 
               the funds in the wallet.  To move the funds, 
               use the &quot;&lt;i&gt;Offline Transactions&lt;/i&gt;&quot; button on the main 
               window for directions (which involves bringing the transaction 
               to this computer for a signature).  Or you can give the
               watching-only wallet to someone who needs to monitor the wallet
               but should not be able to move the money.
               &lt;br&gt;&lt;br&gt;
               Click the button to save a watching-only copy of this wallet.
               Use the &quot;&lt;i&gt;Import or Restore Wallet&lt;/i&gt;&quot; button in the
               upper-right corner</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>context</name>
    <message>
        <location filename="qtdialogs.py" line="12163"/>
        <source>Invalid Code</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12163"/>
        <source>
            You didn't enter a full SecurePrintâ¢ code.  This
            code is needed to decrypt your backup file.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12174"/>
        <source>Bad SecurePrintâ¢ Code</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12168"/>
        <source>
            The SecurePrintâ¢ code you entered has an error
            in it.  Note that the code is case-sensitive.  Please verify
            you entered it correctly and try again.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="12174"/>
        <source>
         The SecurePrintâ¢ code you entered has unrecognized characters
         in it.  %1 Only the following characters are allowed: %2</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>dlgChangeOwner</name>
    <message>
        <location filename="qtdialogs.py" line="2396"/>
        <source>This wallet is mine</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2399"/>
        <source>
               The funds in this wallet are currently identified as
               belonging to &lt;b&gt;&lt;i&gt;you&lt;/i&gt;&lt;/b&gt;.  As such, any funds
               available to this wallet will be included in the total
               balance displayed on the main screen.  


               If you do not actually own this wallet, or do not wish
               for its funds to be considered part of your balance,
               uncheck the box below.  Optionally, you can include the
               name of the person or organization that does own it.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2419"/>
        <source>
               The funds in this wallet are currently identified as
               belonging to &lt;i&gt;&lt;b&gt;%1&lt;/b&gt;&lt;/i&gt;.  If these funds are actually
               yours, and you would like the funds included in your balance in
               the main window, please check the box below.

</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2428"/>
        <source>
               You might choose this option if you keep a full
               wallet on a non-internet-connected computer, and use this
               watching-only wallet on this computer to generate addresses
               and monitor incoming transactions.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2439"/>
        <source>Wallet owner (optional):</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2447"/>
        <source>Set Wallet Owner</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>dlgWarn</name>
    <message>
        <location filename="qtdefines.py" line="511"/>
        <source>&amp;Yes</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdefines.py" line="512"/>
        <source>&amp;No</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdefines.py" line="520"/>
        <source>&amp;Cancel</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdefines.py" line="522"/>
        <source>&amp;OK</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdefines.py" line="573"/>
        <source>Do not show this message again</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdefines.py" line="576"/>
        <source>Do not ask again</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdefines.py" line="579"/>
        <source>Do not show this warning again</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="qtdialogs.py" line="2454"/>
        <source>Careful!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2454"/>
        <source>Armory is not online yet, and will eventually need to be online toaccess any funds sent to your wallet.  Please &lt;u&gt;&lt;b&gt;do not&lt;/b&gt;&lt;/u&gt;receive Bitcoins to your Armory wallets until you have successfullygotten online &lt;i&gt;at least one time&lt;/i&gt;.&lt;br&gt;&lt;br&gt;Armory is still beta software, and some users report difficultyever getting online.&lt;br&gt;&lt;br&gt;Do you wish to continue?</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>parent</name>
    <message>
        <location filename="qtdefines.py" line="186"/>
        <source>Standard User</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdefines.py" line="188"/>
        <source>Advanced User</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdefines.py" line="190"/>
        <source>Expert User</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2487"/>
        <source>This is not your wallet!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2473"/>
        <source>You are getting an address for a wallet thatdoes not appear to belong to you.  Any money sent to thisaddress will not appear in your total balance, and cannotbe spent from this computer.&lt;br&gt;&lt;br&gt;If this is actually your wallet (perhaps you maintain the fullwallet on a separate computer), then please change the&quot;Belongs To&quot; field in the wallet-properties for this wallet.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2487"/>
        <source>Do not show this warning again</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="2487"/>
        <source>You are getting an address for a wallet that you have specified belongs to you, but you cannot actually spend the funds from this computer.  This is usually the case when you keep the full wallet on a separate computer for security purposes.&lt;br&gt;&lt;br&gt;If this does not sound right, then please do not use the following address.  Instead, change the wallet properties &quot;Belongs To&quot; field to specify that this wallet is not actually yours.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7468"/>
        <source>Unlock Paper Backup</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7471"/>
        <source>Unlock Failed</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7471"/>
        <source>
            The wallet could not be unlocked.  Please try again with
            the correct unlock passphrase.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7481"/>
        <source>
         If the backup was printed with SecurePrintâ¢, please
         make sure you wrote the SecurePrintâ¢ code on the
         printed sheet of paper. Note that the code &lt;b&gt;&lt;u&gt;is&lt;/u&gt;&lt;/b&gt;
         case-sensitive!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7488"/>
        <source>
         If the backup was created with SecurePrintâ¢, please
         make sure you wrote the SecurePrintâ¢ code on each
         fragment (or stored with each file fragment). The code is the
         same for all fragments.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7494"/>
        <source>Verify Your Backup!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7494"/>
        <source>
      &lt;b&gt;&lt;u&gt;Verify your backup!&lt;/u&gt;&lt;/b&gt;
      &lt;br&gt;&lt;br&gt;
      If you just made a backup, make sure that it is correct!
      The following steps are recommended to verify its integrity:
      &lt;br&gt;
      &lt;ul&gt;
         &lt;li&gt;Verify each line of the backup data contains &lt;b&gt;9 columns&lt;/b&gt;
         of &lt;b&gt;4 letters each&lt;/b&gt; (excluding any &quot;ID&quot; lines).&lt;/li&gt;
         &lt;li&gt;%1&lt;/li&gt;
         &lt;li&gt;Use Armory's backup tester to test the backup before you
             physically secure it.&lt;/li&gt;
      &lt;/ul&gt;
      &lt;br&gt;
      Armory has a backup tester that uses the exact same
      process as restoring your wallet, but stops before it writes any
      data to disk.  Would you like to test your backup now?
      </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7630"/>
        <source>Bad Public Key</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7630"/>
        <source>Public key data was not recognized</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7639"/>
        <source>Bad Signature</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7639"/>
        <source>Signature data is malformed!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7647"/>
        <source>Address Mismatch</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="7647"/>
        <source>!!! The address included in the signature block does not
         match the supplied public key!  This should never happen,
         and may in fact be an attempt to mislead you !!!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8648"/>
        <source>Select</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8654"/>
        <source>No wallets!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8654"/>
        <source>You have no wallets so
            there is no address book to display.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="8665"/>
        <source>Select from Address Book</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13697"/>
        <source>Recovery Test</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13697"/>
        <source>
         From the data you entered, Armory calculated the following
         wallet ID: &lt;font color=&quot;blue&quot;&gt;&lt;b&gt;%1&lt;/b&gt;&lt;/font&gt;
         &lt;br&gt;&lt;br&gt;
         Does this match the wallet ID on the backup you are
         testing?</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13725"/>
        <source>Bad Backup!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13705"/>
        <source>
            If this is your only backup and you are sure that you entered
            the data correctly, then it is &lt;b&gt;highly recommended you stop using
            this wallet!&lt;/b&gt;  If this wallet currently holds any funds,
            you should move the funds to a wallet that &lt;u&gt;does&lt;/u&gt;
            have a working backup.
            &lt;br&gt;&lt;br&gt; &lt;br&gt;&lt;br&gt;
            Wallet ID of the data you entered: %1 &lt;br&gt; </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13738"/>
        <source>Backup is Good!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13715"/>
        <source>
            &lt;b&gt;Your backup works!&lt;/b&gt;
            &lt;br&gt;&lt;br&gt;
            The wallet ID is computed from a combination of the root
            private key, the &quot;chaincode&quot; and the first address derived
            from those two pieces of data.  A matching wallet ID
            guarantees it will produce the same chain of addresses as
            the original.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13725"/>
        <source>
            If you are sure that you entered the backup information
            correctly, then it is &lt;b&gt;highly recommended you stop using
            this wallet!&lt;/b&gt;  If this wallet currently holds any funds,
            you should move the funds to a wallet that &lt;u&gt;does&lt;/u&gt;
            have a working backup.
            &lt;br&gt;&lt;br&gt;
            Computed wallet ID: %1 &lt;br&gt;
            Expected wallet ID: %2 &lt;br&gt;&lt;br&gt;
            Is it possible that you loaded a different backup than the
            one you just made? </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13738"/>
        <source>
            Your backup works!
            &lt;br&gt;&lt;br&gt;
            The wallet ID computed from the data you entered matches
            the expected ID.  This confirms that the backup produces
            the same sequence of private keys as the original wallet!
            &lt;br&gt;&lt;br&gt;
            Computed wallet ID: %1 &lt;br&gt;
            Expected wallet ID: %2 &lt;br&gt;
            &lt;br&gt;
            </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13754"/>
        <source>
         Please make sure that any printed backups you create  (excluding any &quot;ID&quot; lines) have &lt;b&gt;nine
         columns&lt;/b&gt; of four letters each
         each.
         If you just made a paper backup, it is important that you test it
         to make sure that it was printed or copied correctly.  Most importantly,
         </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13763"/>
        <source>Test Your Backup!</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdialogs.py" line="13763"/>
        <source>
      </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="613"/>
        <source>Export Lockbox Definition</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="MultiSigDialogs.py" line="614"/>
        <source>
      &lt;b&gt;&lt;font color=&quot;%1&quot;&gt;IMPORTANT:&lt;/font&gt;
      All labels and descriptions you have entered for 
      this lockbox are included in this text block below!&lt;/b&gt;  
      &lt;br&gt;&lt;br&gt;
      Before you send this to any other parties, &lt;em&gt;please&lt;/em&gt; confirm
      that you have not entered any sensitive or embarassing information 
      into any of the lockbox fields.  Each lockbox has a name and 
      extended information, as well as a comment for each public key.
      &lt;br&gt;&lt;br&gt;
      All parties or devices that have [partial] signing authority
      over this lockbox need to import this data into their local 
      lockbox manager in order to use it.</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>self.main</name>
    <message>
        <location filename="armorymodels.py" line="496"/>
        <source>&lt;a href=edtBlock&gt;Block:&lt;/a&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="502"/>
        <source>&lt;a href=edtDate&gt;Date:&lt;/a&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="armorymodels.py" line="508"/>
        <source>&lt;a href=goToTop&gt;Top&lt;/a&gt;</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="AddressTypeSelectDialog.py" line="128"/>
        <source>Address Type: </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="AddressTypeSelectDialog.py" line="142"/>
        <source>&lt;u&gt;&lt;font color=&apos;blue&apos;&gt;%1&lt;/font&gt;&lt;/u&gt;</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>self.parent_qobj</name>
    <message>
        <location filename="TreeViewGUI.py" line="350"/>
        <source>Used Addresses</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TreeViewGUI.py" line="351"/>
        <source>Change Addresses</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="TreeViewGUI.py" line="352"/>
        <source>Unused Addresses</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>wizard</name>
    <message>
        <location filename="Wizards.py" line="218"/>
        <source>Shuffle a deck of cards</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="221"/>
        <source>Step 1: Add Manual Entropy</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="222"/>
        <source>
            Use a deck of cards to get a new random number for your wallet.
            </source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="238"/>
        <source>Step 1: Create Wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="239"/>
        <source>
            Create a new wallet for managing your funds.
            The name and description can be changed at any time.</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="267"/>
        <source>Set Passphrase</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="270"/>
        <source>Step 2: Set Passphrase</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="285"/>
        <source>Verify Passphrase</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="289"/>
        <source>Step 3: Verify Passphrase</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="306"/>
        <source>Backup Wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="310"/>
        <source>Step 4: Backup Wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="321"/>
        <source>Create Watching-Only Wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="324"/>
        <source>Step 5: Create Watching-Only Wallet</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="Wizards.py" line="402"/>
        <source>Create Transaction</source>
        <translation type="unfinished"/>
    </message>
</context>
<context>
    <name>wndw</name>
    <message>
        <location filename="qtdefines.py" line="249"/>
        <source>Offline</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdefines.py" line="251"/>
        <source>Watching-Only</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdefines.py" line="253"/>
        <source>Encrypted</source>
        <translation type="unfinished"/>
    </message>
    <message>
        <location filename="qtdefines.py" line="255"/>
        <source>No Encryption</source>
        <translation type="unfinished"/>
    </message>
</context>
</TS>