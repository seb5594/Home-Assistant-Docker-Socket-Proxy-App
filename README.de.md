[🇬🇧 English](README.md) · **🇩🇪 Deutsch**

Lesender Zugriff auf die Docker-API über TCP für Home Assistant. Gedacht für
[Monitor Docker](https://github.com/ualex73/monitor_docker), das Docker nur über einen Socket oder TCP-Port
erreicht. Alles andere bleibt draußen: kein Starten, kein Stoppen, keine Logs, keine Shell.

Die ganze App ist eine kleine C-Datei. Sie wird zu einer statischen Binärdatei von rund 40 KB und braucht unter 1 MB
Arbeitsspeicher, ist also auch für alte 32-Bit-Hardware leicht genug.

## Schnellstart

1. Füge das [App-Repository](https://my.home-assistant.io/redirect/supervisor_add_addon_repository/?repository_url=https%3A%2F%2Fgithub.com%2Fseb5594%2FHome-Assistant-Apps)
   hinzu und installiere **Docker Socket Proxy**.
2. Schalte auf dem Reiter **Info** den **Schutzmodus** aus und starte die App.
3. Sage Monitor Docker, wo der Proxy zu finden ist (den Hostnamen zeigt der Reiter Info):

   ```yaml
   monitor_docker:
     - name: Docker
       url: http://a0d7b954-docker-socket-proxy:5594
       switchenabled: false
   ```

Alle Details, die Liste der erlaubten Endpunkte und Hilfe bei Problemen stehen in der [App-Dokumentation](docker-socket-proxy/DOCS.md).

## Warum der Schutzmodus aus sein muss

Home Assistant bindet den Docker-Socket nur ein, wenn `docker_api` gesetzt **und** der Schutzmodus aus ist. Das erzwingt
der Supervisor, keine App kann es umgehen. Stattdessen hält die App die Angriffsfläche klein: Der Socket bleibt in der App,
nach außen gibt es nur eine kurze Liste erlaubter `GET`-Endpunkte.

## Wie die App lesend bleibt

- Nur `GET` und `HEAD` kommen durch, alles andere bekommt `405`.
- Der Pfad muss einer von wenigen Status-Endpunkten sein, alles andere bekommt `403`. Logs, Archive, Export, Exec und Attach
  fehlen bewusst, weil schon ein einfaches `GET` darauf Dateien lesen oder eine Shell im Container öffnen kann.
- Weitergegeben werden nur Methode, Pfad und Query, in einer Anfrage, die der Proxy selbst baut. Header und Inhalte des Clients
  werden verworfen.

Das prüft [`runtime.sh`](.github/tests/runtime.sh) gegen das gebaute Image, auch mit fehlerhaften und feindlichen Anfragen.

## Unterstützte Architekturen

`amd64`, `aarch64`, `armv7`, `armhf` und `i386`. Die älteren Architekturen werden auf dem
[Multi-Architektur-Base-Image](https://github.com/seb5594/home-assistant-base-multi-arch) gebaut, weil die offiziellen
Home-Assistant-Base-Images sie nicht mehr enthalten.
