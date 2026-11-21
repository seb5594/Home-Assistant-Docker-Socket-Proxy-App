# Docker Socket Proxy

Gives other apps and integrations read-only access to the Docker API over TCP, without handing
them the Docker socket itself. Built for [Monitor Docker](https://github.com/ualex73/monitor_docker).

## Setup

1. Open the **Info** tab of this app and turn **Protection mode** off.
   Home Assistant only mounts the Docker socket into apps that run without it; there is no way around that.
2. Start the app. The log should say `Serving the Docker API read-only on port 5594`.
3. Note the **Hostname** shown on the Info tab, for example `a0d7b954-docker-socket-proxy`.
4. Point Monitor Docker at the proxy in your `configuration.yaml` and restart Home Assistant:

   ```yaml
   monitor_docker:
     - name: Docker
       url: http://a0d7b954-docker-socket-proxy:5594
       switchenabled: false
   ```

   `switchenabled: false` hides the start/stop switches, because the proxy refuses them (see below).

## What is allowed

Only plain `GET` and `HEAD` requests to these endpoints. A version prefix such as `/v1.47` is accepted.

| Endpoint | Purpose |
| --- | --- |
| `/_ping`, `/version`, `/info` | Daemon status |
| `/events` | Live container events |
| `/containers/json` | Container list |
| `/containers/<name>/json` | Container details |
| `/containers/<name>/stats` | CPU, memory, network and disk figures |
| `/images/json` | Image list |

Everything else is answered with `403`, every other method with `405`; neither reaches Docker.
Logs, file downloads, exec and attach are deliberately not on the list, as a plain `GET` on them
can already read files or open a shell inside a container. Refused requests show up in the app log
as `[REJECTED]`.

## Network

| Port | Description |
| --- | --- |
| `5594/tcp` | Docker API, read-only |

Other apps and Home Assistant reach the proxy through the hostname, so the port stays closed to
your network by default. Only enter a host port (for example `1337`) under **Network** if another
device has to use the proxy.

## Good to know

- Even read-only, the container details include environment variables, which can hold passwords.
  Treat the port like a key and do not expose it to networks you do not trust.
- Start, stop and restart buttons of Monitor Docker do not work; the proxy answers them with an error.
- Without a Docker socket the proxy still starts. Every request is then answered with `502` and
  the message `Docker socket not available, turn off Protection mode of this app`.
