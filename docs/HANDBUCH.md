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
| LFO | RATE, AMOUNT, SHAPE, DEST |
| EDIT | POLY MOD: FILT ENV und OSC 2 (bipolar) auf OSC 1 (FREQ, SHAPE, PW) und FILTER (LPF, HPF) |
| FX | FX A: CHORUS, RATE, DEPTH, DISTORT; nochmal FX B: TYPE (ROOM, HALL, PLATE, SPRING, SPACE), MIX, DECAY, TONE; nochmal SPACE: MODULATE, SHIMMER, SWELL, PREDELAY |
| GLO | VOICE: GLIDE, VELOCITY, UNISON, BEND; nochmal CLOCK: BPM, SWING, KEY VEL, MIDI CH (dort auch die CPU-Last) |
| ARP | Arp an + Seite (MODE, RATE, OCTAVES, GATE); nochmal: aus. 1 s halten: HOLD |
| SEQ | SEQUENCER: BARS (1-16), RATE, GATE, TRANSPOSE |
| PLAY | Sequencer von vorn / Stopp |
| REC | Aufnahme: gestoppt Schritt für Schritt, laufend live. 1 s halten: Sequenz löschen |
| SAVE | bearbeitetes Preset in seinem Platz speichern; halten + PRESET: in einen anderen Platz (beim Loslassen). Leuchtet SAVE, ist etwas geändert und noch nicht gespeichert |

## Sequencer

- **Step-Record**: gestoppt REC drücken. Einen Akkord (oder einen Ton) spielen – sobald alle Finger los sind, steht er
  im Step und der Cursor geht weiter. PRESET bewegt den Cursor, SEQ = Pause, SAVE = Tie (Haltebogen). REC beendet.
- **Live-Record**: während er läuft REC drücken; was du spielst, landet im nächstgelegenen Step.
- Bis zu 16 Takte à 16 Steps, je Step bis zu 7 Töne. Speichert sich selbst, wenn gestoppt.

## Poly Mod, Vintage, Spread

- **POLY MOD** wie beim Prophet: Filterhüllkurve und Oszillator 2 (auch im Audiobereich: FM, Filter-FM) modulieren
  Tonhöhe, Wellenform und Pulsbreite von Oszillator 1 und die Filter. Mit OSC 2 auf LOW FRQ wird Oszillator 2 zum
  LFO pro Stimme. Probier PM BELLS, SYNC LEAD, PM GROWL, GLASS PAD.
- **VINTAGE** (SELECT): 0 = sieben identische Stimmen; hoch = jede Stimme mit eigener Stimmung, Drift, Filter,
  Hüllkurven und Lautstärke – fest pro Stimme, nicht zufällig, damit Akkorde stimmig bleiben.
- **SPREAD** (ALGORITHM): die Stimmen von Mono zu breitem Stereo, mit etwas eigener Verstimmung und Farbe pro Stimme.

## MIDI

Eingang (USB und TRS): Noten mit Anschlagstärke, Pitch Bend, CC 1 (Modrad: mehr LFO), CC 64 (Sustain), CC 74 (Cutoff),
Clock mit START/STOP/CONTINUE. Ausgang (USB): die Noten der Tasten.

## Bitte testen

Auf der CLOCK-Seite (GLO zweimal) steht die CPU-Last. Bitte mit schweren Presets (PM GROWL, GLASS PAD, SPACE DRIFT)
und sieben gehaltenen Tönen prüfen.
