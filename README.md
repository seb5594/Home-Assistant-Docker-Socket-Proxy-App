<!-- header:begin (generated from project metadata) -->
<h1 align="center">Docker Socket Proxy</h1>

<p align="center">
  <img src="https://raw.githubusercontent.com/seb5594/Home-Assistant-Docker-Socket-Proxy-App/main/docker-socket-proxy/logo.png" alt="Docker Socket Proxy" width="360" height="90">
</p>

---

<p align="center">
  <a href="https://paypal.me/seb5594"><img src="https://img.shields.io/static/v1?label=Support&amp;message=PayPal&amp;color=0070BA&amp;logo=paypal&amp;logoColor=white&amp;style=for-the-badge" alt="PayPal" height="40"></a>
  <a href="https://buymeacoffee.com/seb5594"><img src="https://raw.githubusercontent.com/seb5594/Home-Assistant-Apps/473dc258f963096745aac327df54950eb9b59066/.github/assets/support-beer.svg" alt="Buy me a beer" height="40"></a><br>
  <a href="https://my.home-assistant.io/redirect/supervisor_add_addon_repository/?repository_url=https%3A%2F%2Fgithub.com%2Fseb5594%2FHome-Assistant-Apps"><img src="https://img.shields.io/static/v1?label=Home%20Assistant&amp;message=Add%20app%20repository&amp;color=18BCF2&amp;logo=homeassistant&amp;logoColor=white&amp;style=for-the-badge" alt="Home Assistant" height="28"></a>
  <a href="https://github.com/seb5594/Home-Assistant-Docker-Socket-Proxy-App/releases"><img src="https://img.shields.io/static/v1?label=version&amp;message=2026.11.21&amp;color=1877A5&amp;style=for-the-badge" alt="version" height="28"></a><br>
  <img src="https://img.shields.io/static/v1?label=arch&amp;message=amd64%20%C2%B7%20aarch64%20%C2%B7%20armv7%20%C2%B7%20armhf%20%C2%B7%20i386&amp;color=157F71&amp;style=flat" alt="arch" height="20">
  <img src="https://img.shields.io/static/v1?label=stage&amp;message=stable&amp;color=2F855A&amp;style=flat" alt="stage" height="20">
  <img src="https://img.shields.io/static/v1?label=docker%20socket%20%28read-only%29&amp;message=5594%2Ftcp&amp;color=157F71&amp;style=flat" alt="docker socket (read-only)" height="20"><br>
  <a href="https://github.com/seb5594/Home-Assistant-Docker-Socket-Proxy-App/stargazers"><img src="https://img.shields.io/github/stars/seb5594/Home-Assistant-Docker-Socket-Proxy-App?label=stars&amp;style=flat" alt="stars" height="20"></a>
  <a href="https://github.com/seb5594/Home-Assistant-Docker-Socket-Proxy-App/issues"><img src="https://img.shields.io/github/issues/seb5594/Home-Assistant-Docker-Socket-Proxy-App?label=issues&amp;style=flat" alt="issues" height="20"></a>
  <a href="https://github.com/seb5594/Home-Assistant-Docker-Socket-Proxy-App/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/seb5594/Home-Assistant-Docker-Socket-Proxy-App/ci.yml?branch=main&amp;label=checks&amp;style=flat" alt="checks" height="20"></a>
  <a href="https://github.com/seb5594/Home-Assistant-Docker-Socket-Proxy-App#license"><img src="https://img.shields.io/github/license/seb5594/Home-Assistant-Docker-Socket-Proxy-App?label=license&amp;style=flat" alt="license" height="20"></a>
</p>
<!-- header:end -->

**🇬🇧 English** · [🇩🇪 Deutsch](README.de.md)

Read-only access to the Docker API over TCP, for Home Assistant. It is made for
[Monitor Docker](https://github.com/ualex73/monitor_docker), which can only talk to Docker through a
socket or a TCP port, and it keeps everything else out: no start, no stop, no logs, no shell.

The whole app is one small C file. It compiles to a static binary of roughly 40 KB and needs under 1 MB of
memory, so it is light enough for old 32-bit hardware as well.

## Quick start

1. Add the [app repository](https://my.home-assistant.io/redirect/supervisor_add_addon_repository/?repository_url=https%3A%2F%2Fgithub.com%2Fseb5594%2FHome-Assistant-Apps)
   and install **Docker Socket Proxy**.
2. On the **Info** tab, turn **Protection mode** off and start the app.
3. Tell Monitor Docker where to find it (the hostname is shown on the Info tab):

   ```yaml
   monitor_docker:
     - name: Docker
       url: http://a0d7b954-docker-socket-proxy:5594
       switchenabled: false
   ```

Every option, the list of allowed endpoints and troubleshooting are in the [app documentation](docker-socket-proxy/DOCS.md).

## Why Protection mode has to be off

Home Assistant only mounts the Docker socket into an app when `docker_api` is set **and** Protection mode is
off. The Supervisor enforces this, so no app can work around it. What this app does instead is shrink the
exposure: the socket stays inside the app, and the outside only gets a short allowlist of `GET` endpoints.

## How it stays read-only

For every connection the proxy reads one HTTP request and checks it:

- Only `GET` and `HEAD` pass. Everything else gets `405`.
- The path must be one of a handful of status endpoints (list, details and stats of containers, events, version,
  info, images). Everything else gets `403`. Logs, archive, export, exec and attach are not on the list because a
  plain `GET` on them can read files or open a shell in a container.
- Only method, path and query are forwarded, in a request the proxy builds itself. Client headers and bodies are
  dropped, so request smuggling has nothing to work with.

The checks are covered by [`runtime.sh`](.github/tests/runtime.sh), which runs against the built image in CI and
also tries malformed and hostile requests.

## Supported architectures

`amd64`, `aarch64`, `armv7`, `armhf` and `i386`. The legacy architectures are built on
[the multi-architecture base image](https://github.com/seb5594/home-assistant-base-multi-arch), because the
official Home Assistant base images dropped them.

## Building by hand

```bash
docker build -t docker-socket-proxy docker-socket-proxy/
docker run --rm --entrypoint bash -v "$PWD/.github/tests/runtime.sh:/runtime.sh:ro" docker-socket-proxy /runtime.sh
```

The program itself is [`proxy.c`](docker-socket-proxy/proxy.c); `gcc proxy.c -o proxy` is all it takes.

<!-- support:begin (generated, shared text) -->
## Say thanks

All my projects are built in my spare time, just for the fun of it. Still, they take a lot of time and care, and many of the smart home gadgets I buy end up here because I want to make them cloud-free and smart-home ready. Every bit of support means a lot to me.

If my work helped you or turned out to be useful, show your appreciation with a hot drink for my next sleepless night.

[![PayPal](https://img.shields.io/static/v1?label=Support&message=PayPal&color=0070BA&logo=paypal&logoColor=white&style=for-the-badge)](https://paypal.me/seb5594) [![Buy me a beer](https://raw.githubusercontent.com/seb5594/Home-Assistant-Apps/473dc258f963096745aac327df54950eb9b59066/.github/assets/support-beer.svg)](https://buymeacoffee.com/seb5594)
<!-- support:end -->

<!-- license:begin (generated from the license file) -->
## License

[![license](https://img.shields.io/static/v1?label=license&message=MIT&color=5B6770&style=flat)](LICENSE)

Published under the [MIT License](LICENSE).
<!-- license:end -->
