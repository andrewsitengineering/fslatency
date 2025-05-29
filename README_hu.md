# fslatency

forked from https://github.com/maulisadam/perfmeters

## Cél

Minél hamarabb, minél akkurátusabban észrevenni, ha egy diszkrendszer megakad.

- Akkurátusabban: a false esetek minimalizálása elsődleges cél.
- Gyorsan: 5 sec a kitűzött cél.

Mindezt a roppant bonyolult shared storage - -FC - ESX - vm környezetben.

## Architektúra

A rendszer két részből áll. Egy monitoring agent (telepítve sok VM-re) és egy (egyetlen) data processor.
Feltétel, hogy a rendszer diszk/diszkalrendszer lehalás esetén is kis ideig még működjön,
amiatt a riasztások (amit a data processor ad ki magából) nem használhatnak diszket. Technikailag stdout-ról lehet csak szó.
Hasonlóan, a monitoring agent és a data processor között is csak diskless kommunikáció lehet. Technikailag ez UDP-t jelent.
A monitoring agent csak attól a diszktől függjön, amit éppen mér. Ne legyen menet közbeni konfigolvasás sem semmi egyéb open().

Ezek miatt a monitorozo agent C-ben írt, statikusra fordított monolit program.
A data processor lehetne más. Azonban a data processornak olyan helyen kell futnia,
ami oprendszer nem függ a monitorozott diszkalrendszertől, így ez nálunk a fizikai tűzfalak;
ami szintén maga után vonja, hogy statikusra fordított monolitikus program kell, hogy legyen.
Nem lehet a névfeloldást statikusra fordítani (libc-dependent) emiatt nincs névfeloldás: IP címet kell megadni.


### monitoring agent

    fslatency --serverip a.b.c.d [--serverport PORT] [--text "FOO"] --file /var/lib/fslatency/check.txt
    [--nocheckfs] [--nomemlock] [--debug] [--version]

Ahol is

- a.b.c.d  az IP címe a data procssornak. Ide küldi az UDP csomagokat. No default
- PORT  Az UDP port címe a dta processornak. Default: 57005 (0xDEAD)
- "FOO" freetext, amit elküld az UDP csomagokban. Ez opcionális. A hostname értékét mindenképpen elküldi az UDP csomagokban. Ezáltal lehetsége pl egy VM-en futó két monitoring agentet megkülönböztetni (ha pl. két diszet is szeretnénk monitorozni). Max 63 karakter.
- file: egy konkrét filename, ami valódi blockdevice-n lévő valódi filesystemen van van. Tehát NEM tmpfs, NEM nfs és NEM fuse. Ezt a file-t rendszeresen írja/zája, törli, létrehozza.
- --nocheckfs Nem ellenörzi, hogy a megadott file lokális filesystemen van-e. Ne használd.
- --nomemlock Nem lockolja be a memóriába a processz lapjait. Default: belockolja.
- --debug
- --version

#### Architectura:

Két pthread, az egyik a file folyamatos írásával méri a filesystem/diszkalrendszer teljes reagálási idejét.
A másik szál ezt figyeli, és rendszeresen (másodpercenként) ebből egy rövid jelentést küld a data processornak.

syslog/stdout -ra csak indításkor ír, és ha valid filesystem hibát kap (diszk teli, nincs jog stb). Leakadás, behalás és egyebek esetén meg sem próbál lokálisan írni.


### data processor

    Usage: fslatency_server [--bind a.b.c.d] [--port PORT] [--maxclient 509]
       [--timetoforget 600] [--udptimeout 3] [--alarmstatusperiod 1]
       [--statusperiod 300] [--alarmtimeout 8] [--latencythresholdfactor 15.0]
       [--rollingwindow 60] [--minimummeasurementcount 60]
       [--graphitebase metric.path.base --graphiteip 1.2.3.4 [--graphiteport 2003]]
       [--nomemlock] [--debug[=1]] [--version]


Ahol is

- --bind a.b.c.d az IP címe az inteface-nek, amin figyelni kell. Default: 0.0.0.0 (minden)
- --port PORT Az UDP port címe, amin figyel. Default: 57005 (0xDEAD)
- --maxclient Integer. A belső kliens-tábla mérete. A program NEM dinamikus, ez induláskor foglalódik. Több klienst nem tud. Default: 509 (egy kedves prím)
- --timetoforget Integer, másodperc. Mennyi idő alatt felejtse el a klienst, aki nem küld adatot. Default: 600 (10 perc, nem prím, de legalább kerek)
- --udptimeout Integer, másodperc. Mennyi idő alatt tekintse elveszettnek egy klienst (riasztási esemény).
- --alarmstatusperiod Integer, másodperc. Ha riasztás van, akkor mennyi időnként írjon ki státuszt. Default 1 sec. Nem pontos érték.
- --statusperiod Integer, másodperc. Ha nincs riasztás, akkor menny időnként írjon ki státuszt. Default 300 (5 perc). Nem pontos érték.
- --alarmtimeout Integer, másodperc. mennyi idő alatt felejtse el a riasztást (ha nem volt újabb). Default 8. Ez akadályozza meg a flipflop esetén a riasztási floodot.
- --latencythresholdfactor float. Ha a kliens által jelzett latency eltér a korábbiak átlagától a szorás ennyi szeresénél jobban, akkor riaszt. Default: 15. Ez a dolog kicsit matekos. Lényeg az, ha ezt a küszöböt emeled, csökken a fals riasztások száma.
- --rollingwindow Integer, másodperc/darab. Maximum csomagnyi adatból végezze a statisztikai riasztást. Default: 60.
- --minimummeasurementcount Integer, darab. Minimum ennyi mérésnek kell meglennie, hogy a statisztikai riasztó jelezzen. Default: 60 mérés (cca 5-6 sec)
- --graphitebase String. Ha meg van adva, akkor gatewayként elküldi egy graphite szervernek az adatokat olyan outputot ad graphite(carbon) plaintext input formában.
- --graphiteip 1.2.3.4 az IP címe a graphite szrevernek (no default). Csak akkor veszi figyelembe, ha --graphitebase nem nulla.
- --graphiteport 2003. A graphite szrever plaintex inputjának tcp portja. Default: 2003.
- --nomemlock Nem lockolja be a memóriába a processz lapjait. Default: belockolja.
- --debug some global debug info, no flood
- --debug=2 additional debug for each packet
- --debug=3 additional debug for packet's data


A data processor automatikusan felveszi a figyelendő agentek közé, akitől kap UDP csomagot.
És egyből riaszt is, ha nem kap tőle többet. A riasztás egynél többször nem jön ki, csak a rendszeres státuszjelentésnél látszik.

Ha azonban "timetoforget" ideig (10 perc) egyetlen csomagot sem kap attól, akkor eltávolítja a figyelendő agentek listájából, és többet nem riaszt rá.

Jelzések (minden az stdout -on)

- új agent jelentkezik
- agent eltávolítódik a "timetoforget" miatt

Nem riasztási események, amikre lekezel, de nem jelez:

- agent kihagyott csomagot (az udptimeout nem telt le, de egy csomag kimmaradt a következő csomag bepótolta) "packet loss". A protokol 7 ilyen eltünését tolerálja.

Riasztások (legelső ilyen jön ki), majd "riasztás van" állapotba kerül a data processor

- agent nem küldöt csomagot (udptimeout letelt az utolsó csomag óta) "agent lost"
- agent küldöt csomagot, de abban 0 mérés van (nem tudott mérni) "stuck"
- agent küldött mérést, azonban az irreális (nagyon meghaladja a korábbiakból várató értéket) "bad latency" ebből külön LOW és HIGH

Státusz

- Riasztás állapotban másodpercenként státuszt ír ki
- Nem riasztás állapotban 5 perenként
- Státusz: timestamp, agentek száma, "baj van" agentek száma részletezés: agent lost, not measuring, bad latency, communication error),  lnlatency min/max/mean/std

A latency értékek msec-ben értendők, de mindenhol a logaritmusa szerepel (természetes alapú logaritmus)!


### UDP kommunikáció

We don't do host-to-network (endianess) transformation, so monitoring agent and data processor must be running same architecture.


- "fslatency      \0" fix string  (16 byte)
- kommunikáció verzió 32 bit (16 bit major, 16 bit minor);
- hostname (64 karakter, '\0' filled)
- text (64 karakter '\0' filled) lásd a monitoring agent --text opciót
- measuring precision struct timespec == 64 bit
- last 1 sec datablock
- 2nd previous 1sec datablock
- 3rd previous 1sec datablock
- 4th previous 1sec datablock
- 5th previous 1sec datablock
- 6th previous 1sec datablock
- 7th previous 1sec datablock
- 8th previous 1sec datablock

Datablock:

- number of measurements (integer, 64 bit)
- starttime  struct timespec == 64 bit (unix time: from UTC epoch)
- endtime  struct timespec == 64 bit
- min (float 64bit)
- max (float 64bit)
- sumX (float 64bit) sum of all measurements in this interval
    can be used to calculate the average
- sumXX (float 64bit) sum of all measurements² in this interval
    can be used to calculate std deviation

### Egy kis matek

Azért számolunk átlagot és szórást, hogy automatikusan meg lehessen állapítani, hogy egy kilógó érték az "még belefér" vagy már nem.
Nem akartam fix küszöbértékeket alkalmazni, ugyanis különböző diszkalrendszereknél más és mást jelent a "még belefér" fogalma.
Sajnos azonban a latency eloszlását nem ismerem. Végeztem egy-két mérést, és úgy tünik, hogy nagyon-nagyon nem normáleloszlás.
Kilógó nagyon magas latency értékek üzemszerűen előfordulnak egyszer-kétszer. És egésznek volt egy exponenciális jellegzetssége.

Exponenciális elolszlás? Nem, mert határozottan van a sűrűségfüggvénynek felfutása.
Lognorm? Nem egészen. Annál lassabban csillapodik, de gyorsabban indul. Leginkább k=2 Khi-négyzet eloszlás jellege van, de annál is lassabban csillapodik.

- Ezek miatt a mérési adatokat legelöbb felbontást váltok: a nanosec felbontást millisec-re váltom. Ez az érték a nagyságrendje átlaga egy modern SSD filesystem műveletnek.
- A kapott értéknek a logaritmusát veszem. Ugyan az eloszlás még mindig messze van a normálolosztástól (nagyon hosszú a farka), de már kezelhető.
- Nem akartam adattisztás címén a kevés darab, de nagyon kilógó adatot levágni. Kifejezetten kiváncsi vagyok az egyszer csak megjelenő egyszem kilógó adatra. Nem csak az emelkedő átlagra, hanem kifejezetten az egyszem kilógásra is.
- Ezek után lett a programban a logaritmus után végzett stddev, és az stddev 15-szörösével számolás.

Az egészet megfejeli egy riasztás akkor, ha üres csomag jött (stuck). Ez automatikusan hozzátesz a riasztáshoz egy felső küszöböt: Ha a latency meghaladja az 1 másodpercet, akkor mindenképp riaszt.
