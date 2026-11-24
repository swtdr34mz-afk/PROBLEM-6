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

Eingang (USB und TRS): Noten mit Anschlagstärke, Pitch Bend, CC 1 (Modrad: mehr LFO), CC 64 (Sustain), CC 74 (Cutoff),
Clock mit START/STOP/CONTINUE. Ausgang (USB): die Noten der Tasten.

## Bitte testen

Auf der CLOCK-Seite (GLO zweimal) steht die CPU-Last. Bitte mit schweren Presets (PM GROWL, GLASS PAD, SPACE DRIFT)
und sieben gehaltenen Tönen prüfen.
