# PROPHET 67 – Handbuch

Ein 7-stimmiger Analog-Poly für das M-VAVE FM-1, angelehnt an den Sequential Prophet-6. Inoffiziell, Installation auf
eigene Gefahr.

## Bedienung

| Bedienelement | Funktion |
| --- | --- |
| Tasten | spielen; OCT- / OCT+ verschieben um ±3 Oktaven. Bei eingeschaltetem Arp füttern sie den Arp |
| K1..K4 | die vier Regler der Seite (bipolare Regler rasten ein paar Klicks in der Mitte) |
| SELECT | VINTAGE: jede Stimme bekommt ihren eigenen Charakter, von sauber bis „gut gealterter Prophet“ |
| ALGORITHM | VOICE SPREAD: die Stimmen von der Mitte in ein breites Stereobild |
| PRESET | die 16 Presets (im Step-Record: der Step) |
| HOME | FILTER: CUTOFF, RESONANCE, ENV AMT, HP CUTOFF; nochmal FILTER 2: HP RESO, KEYBOARD, VELOCITY, HP ENV. 1 s halten: Preset wie gespeichert |
| SEL | OSC 1: FREQ, SHAPE, PW, SUB OCT; nochmal OSC 2: FREQ, FINE, SHAPE, PW; nochmal MIXER: OSC 1, OSC 2, NOISE, OSC-2-Modus (NORMAL, SYNC, LOW FRQ, LF FIXED) |
| ENV | FILTER ENV (ADSR); nochmal AMP ENV |
| LFO | LFO 1: RATE, AMOUNT, SHAPE, DEST; nochmal LFO 2; nochmal LFO SYNC: SYNC 1, SYNC 2 (OFF oder 4 BARS … 1/32 zum Takt – dann wählt RATE den Notenwert), RETRIG 1, RETRIG 2 (neuer Ton ohne gehaltene Taste startet das LFO von vorn) |
| EDIT | POLY MOD; nochmal LOW SPLIT: SPLIT (aus, links 1–3 Oktaven tiefer), POINT (Splitpunkt; oder SEL halten + Taste), SCALE L, SCALE R (Tonleiter je Hand); nochmal SPLIT KEYS: KEY (Tonart), FOLLOW (KEY / ROOT: rechte Hand folgt dem Grundton der linken / WHITE: Tonleiter auf den weißen Tasten), LOW HOLD (linke Töne bleiben stehen), BALANCE (Lautstärke links/rechts) |
| FX | REVERB: TYPE (ROOM, HALL, PLATE, SPRING, SPACE), MIX, DECAY, TONE; nochmal SPACE REVERB: MODULATE, SHIMMER, SWELL, PREDELAY; nochmal CHORUS: CHORUS, RATE, DEPTH, DISTORT; nochmal DELAY: TIME L, TIME R, REPEATS, MIX; nochmal DELAY MOD: DEPTH, RATE, TONE, SYNC (Zeiten als Notenwerte zum Takt) |
| GLO | VOICE: GLIDE, VELOCITY, UNISON, BEND; nochmal AFTERTOUCH: DEST (FILTER, SWELL, POWER, VIBRATO, LFO 1, PITCH, POLY MOD), AMOUNT (±), CURVE (früh/spät), SMOOTH (wie weich der Druck ankommt); nochmal CLOCK: BPM, SWING, KEY VEL (TOUCH = Anschlag der Tasten, oder fest), MIDI CH (dort auch die Last beider Kerne); nochmal METRONOME: CLICK, VOLUME, BPM, SWING |
| ARP | Arp an + Seite (MODE, RATE, OCTAVES, GATE); nochmal: aus. 1 s halten: HOLD |
| SEQ | TRACK: TRACK (1–2), LENGTH (1–64 Steps), RATE, TRANSPOSE; nochmal MUTES & CLICK (Spur 1/2 stumm, Metronom). Im Step-Record: Pause. 1 s halten: Metronom an/aus |
| PLAY | Sequencer von vorn / Stopp |
| REC | Aufnahme in die gewählte Spur: gestoppt Schritt für Schritt (Seite STEP EDIT), laufend live. 1 s halten: Spur löschen |
| SAVE | bearbeitetes Preset in seinem Platz speichern; halten + PRESET: in einen anderen Platz (beim Loslassen). Leuchtet SAVE, ist etwas geändert und noch nicht gespeichert |

## Sequencer (v1.1): 2 Spuren × 64 Steps

- **Zwei Spuren**, jede mit **eigenem Sound** (ein Preset, beliebig bearbeitet, mit dem Song gespeichert) und eigener
  Linie – z. B. Bass und Akkorde. Die 7 Stimmen teilen sich alle Spuren; jede Stimme klingt mit dem Sound
  der Spur, deren Note sie spielt.
- **Effekte** (Reverb, Space, Chorus, Distortion, Delay) gibt es einmal für den ganzen Song: ein Preset in Spur 1 bringt
  seine Effekte mit, in Spur 2–4 bleiben die des Songs; eine FX-Seite ändert sie für alle Spuren.
- **Pro Spur**: LENGTH 1–64 Steps (verschiedene Längen laufen gegeneinander), RATE (1/8 … 1/32, Triolen), TRANSPOSE
  ±24, MUTE. SWING (CLOCK) gilt für alle. PLAY startet alle von vorn.
- **Pro Step**: ein Ton oder ein Akkord (bis 7 Töne), Pause oder Tie, eigene **Velocity** und **Gate** (5–100 %).
- **SEQ** öffnet die Seite TRACK: K1 wählt die Spur. Die gewählte Spur spielst du auf den Tasten, bearbeitest du mit
  allen Seiten, in sie lädt PRESET einen Sound, SAVE speichert ihren Sound, REC nimmt in sie auf, der Arp spielt sie.
  Die Seite zeigt beide Linien mit Länge und Laufpunkt; unten die 16 Steps der gewählten Spur – je Step so hoch
  wie seine Velocity und so breit wie sein Gate. SEQ nochmal: MUTES & CLICK (Mute Spur 1, Mute Spur 2, Metronom CLICK, VOLUME). Jede Spur bleibt auf dem Takt, auch
  wenn LENGTH oder RATE während der Wiedergabe geändert werden.
- **Step-Record**: gestoppt REC drücken (Seite STEP EDIT). Einen Akkord oder Ton spielen – sobald alle Finger los
  sind, steht er im Step (mit der gespielten Velocity) und der Cursor geht weiter. K1 oder PRESET = Cursor,
  K2 = VELOCITY, K3 = GATE, K4 = NOTE (verschiebt die Töne des Steps), SEQ = Pause, SAVE = Tie. REC beendet.
- **Live-Record**: während er läuft REC drücken; was du spielst, landet im nächstgelegenen Step der gewählten Spur – mit
  der gespielten Velocity und **so lang, wie du die Taste hältst**: innerhalb eines Steps als Gate, länger als Ties über
  die folgenden (freien) Steps, der letzte mit dem Gate, an dem du losgelassen hast.
- **Metronom**: GLO bis zur Seite METRONOME (CLICK: OFF / ON während der Sequencer läuft / REC nur bei Aufnahme,
  VOLUME, BPM, SWING); die Eins des Takts höher. **SEQ 1 s halten** schaltet es an und aus.
- REC 1 s halten löscht die gewählte Spur. Der Song speichert sich selbst, wenn es still ist. Eine Sequenz aus v1.0
  landet in Spur 1 (ihre ersten 64 Steps).

## Poly Mod, Vintage, Spread

- **POLY MOD** wie beim Prophet: Filterhüllkurve und Oszillator 2 (auch im Audiobereich: FM, Filter-FM) modulieren
  Tonhöhe, Wellenform und Pulsbreite von Oszillator 1 und die Filter. Mit OSC 2 auf LOW FRQ wird Oszillator 2 zum
  LFO pro Stimme. Probier PM BELLS, SYNC LEAD, PM GROWL, GLASS PAD.
- **VINTAGE** (SELECT): 0 = sieben identische Stimmen; ab der Mitte deutlich hörbar, ganz oben ein alter Prophet:
  pro Stimme eigene Stimmung (bis etwa ±12/±15 Cent je Oszillator, dazu Drift und ein paar Cent pro Anschlag),
  Filter (±0,6 Oktaven), Resonanz, Hüllkurvenzeiten (±55 %), Lautstärke (±3 dB), Wellenform und Filter-Sättigung –
  fest pro Stimme, nicht zufällig, damit Akkorde stimmig bleiben.
- **SPREAD** (ALGORITHM): die Stimmen von Mono zu breitem Stereo, mit etwas eigener Verstimmung und Farbe pro Stimme.

## MIDI

Eingang (USB und TRS): Noten mit Anschlagstärke, Druck (pro Taste oder Kanal), Pitch Bend, die CCs unten, Clock mit
START/STOP/CONTINUE. Ausgang (USB): die Noten der Tasten.

### MIDI-CC-Liste (ab v1.4)

Fast jeder Regler lässt sich von einem Controller aus steuern. Wo der Prophet-6 denselben Regler hat, ist es seine
CC-Nummer, sodass Prophet-6-Vorlagen für Controller und Editoren direkt passen. Der Rest liegt auf CCs, die der Prophet-6
frei lässt. 0–127 deckt den ganzen Regler ab, bei Auswahl-Reglern (z. B. LFO SHAPE) die Möglichkeiten gleichmäßig
verteilt. Ein CC ändert den Sound der Spur, die gerade angezeigt ist (Sequencer-Spur 1 oder 2), auf dem eingestellten
MIDI-Kanal (GLO → CLOCK → MIDI CH). Danach leuchtet SAVE wie beim Drehen am Gerät.

| CC | Regler | Hinweis |
|---|---|---|
| 1 | MOD WHEEL | mehr LFO 1 |
| 5 | GLIDE | Glide-Zeit (wie Prophet-6) |
| 8 | OSC 1 SUB OCT | Sub-Oszillator (wie Prophet-6) |
| 9 | DISTORT | Verzerrung (wie Prophet-6) |
| 14 | LFO 1 RATE |  |
| 15 | LFO 1 AMOUNT |  |
| 16 | LFO 1 SHAPE | Auswahl |
| 17 | LFO 1 DEST | Auswahl |
| 18 | LFO 2 RATE |  |
| 19 | LFO 2 AMOUNT |  |
| 20 | LFO 2 SHAPE | Auswahl |
| 21 | LFO 2 DEST | Auswahl |
| 22 | POLY MOD FILT ENV |  |
| 23 | POLY MOD OSC 2 |  |
| 24 | POLY MOD OSC 1 TO | Auswahl |
| 25 | POLY MOD FILTER TO | Auswahl |
| 26 | NOISE | Mixer |
| 27 | OSC 2 MODE | normal / Sync / Low Freq |
| 28 | VINTAGE | SELECT |
| 29 | VOICE SPREAD | ALGORITHM |
| 30 | UNISON | aus / an |
| 41 | AMP VELOCITY |  (wie Prophet-6) |
| 43 | AMP ATTACK |  (wie Prophet-6) |
| 44 | AMP DECAY |  (wie Prophet-6) |
| 45 | AMP SUSTAIN |  (wie Prophet-6) |
| 46 | AMP RELEASE |  (wie Prophet-6) |
| 47 | LPF ENV AMOUNT |  (wie Prophet-6) |
| 50 | FILTER ATTACK |  (wie Prophet-6) |
| 51 | FILTER DECAY |  (wie Prophet-6) |
| 52 | FILTER SUSTAIN |  (wie Prophet-6) |
| 53 | FILTER RELEASE |  (wie Prophet-6) |
| 54 | HPF ENV AMOUNT |  (wie Prophet-6) |
| 64 | SUSTAIN | Pedal |
| 67 | OSC 1 FREQ |  (wie Prophet-6) |
| 69 | OSC 1 LEVEL | Mixer (wie Prophet-6) |
| 70 | OSC 1 SHAPE |  (wie Prophet-6) |
| 71 | OSC 1 PW |  (wie Prophet-6) |
| 74 | BRIGHTNESS | wird zum Cutoff addiert (wie Prophet-6) |
| 75 | OSC 2 FREQ |  (wie Prophet-6) |
| 76 | OSC 2 FINE |  (wie Prophet-6) |
| 77 | OSC 2 LEVEL | Mixer (wie Prophet-6) |
| 78 | OSC 2 SHAPE |  (wie Prophet-6) |
| 79 | OSC 2 PW |  (wie Prophet-6) |
| 80 | AFTERTOUCH AMOUNT |  |
| 81 | AFTERTOUCH DEST | Auswahl |
| 85 | CHORUS MIX |  |
| 86 | CHORUS RATE |  |
| 87 | CHORUS DEPTH |  |
| 88 | REVERB TYPE | Auswahl |
| 89 | REVERB MIX |  |
| 90 | REVERB DECAY |  |
| 102 | LPF CUTOFF |  (wie Prophet-6) |
| 103 | LPF RESONANCE |  (wie Prophet-6) |
| 104 | LPF KEYBOARD | aus / halb / voll (wie Prophet-6) |
| 105 | LPF VELOCITY |  (wie Prophet-6) |
| 106 | HPF CUTOFF |  (wie Prophet-6) |
| 107 | HPF RESONANCE |  (wie Prophet-6) |
| 110 | REVERB TONE |  |
| 111 | SPACE MOD |  |
| 112 | SPACE SHIMMER |  |
| 113 | SPACE SWELL |  |
| 114 | SPACE PREDELAY |  |
| 115 | DELAY MIX |  |
| 116 | DELAY FEEDBACK |  |
| 117 | DELAY TIME L |  |
| 118 | DELAY TIME R |  |
| 119 | DELAY TONE |  |
| 120 | ALL SOUND OFF |  |
| 123 | ALL NOTES OFF |  |


## Bitte testen

Auf der CLOCK-Seite (GLO zweimal) steht die CPU-Last. Bitte mit schweren Presets (PM GROWL, GLASS PAD, SPACE DRIFT)
und sieben gehaltenen Tönen prüfen.
