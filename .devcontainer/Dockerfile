FROM ubuntu:26.04 AS base

ARG DEBIAN_FRONTEND=noninteractive
ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=1000
ARG UV_VERSION=0.8.17
# Update this single pin when upgrading ESP-IDF.
ARG ESP_IDF_VERSION=v6.1

ENV DEBIAN_FRONTEND=${DEBIAN_FRONTEND} \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    IDF_PATH=/opt/esp-idf \
    IDF_TOOLS_PATH=/opt/espressif \
    IDF_CCACHE_ENABLE=1 \
    CCACHE_DIR=/home/${USERNAME}/.cache/ccache

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# Generic Ubuntu development tooling and ESP-IDF host prerequisites.
RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        ca-certificates \
        curl \
        wget \
        gnupg \
        openssh-client \
        git \
        git-lfs \
        zsh \
        build-essential \
        cmake \
        ninja-build \
        ccache \
        pkg-config \
        python3 \
        python3-dev \
        python3-pip \
        python3-venv \
        python-is-python3 \
        jq \
        ripgrep \
        fd-find \
        fzf \
        tree \
        less \
        unzip \
        zip \
        tar \
        xz-utils \
        shellcheck \
        clang \
        clang-format \
        clang-tidy \
        cppcheck \
        gdb \
        flex \
        bison \
        gperf \
        libffi-dev \
        libssl-dev \
        libusb-1.0-0 \
        dfu-util \
    && ln -s /usr/bin/fdfind /usr/local/bin/fd \
    && git lfs install --system \
    && rm -rf /var/lib/apt/lists/*

# General Python tooling remains independently managed by uv.
RUN curl --fail --silent --show-error --location "https://astral.sh/uv/${UV_VERSION}/install.sh" \
    | env UV_INSTALL_DIR=/usr/local/bin UV_NO_MODIFY_PATH=1 sh \
    && uv --version

# ESP-IDF and its Python environment are installed by Espressif's installer.
RUN git clone --branch "${ESP_IDF_VERSION}" --depth 1 --recursive \
        https://github.com/espressif/esp-idf.git "${IDF_PATH}" \
    && "${IDF_PATH}/install.sh" esp32,esp32s3,esp32c3,esp32c6 \
    && rm -rf /root/.cache/pip

RUN if getent group "${USER_GID}" >/dev/null; then \
        existing_group="$(getent group "${USER_GID}" | cut -d: -f1)"; \
        groupmod --new-name "${USERNAME}" "${existing_group}"; \
    else \
        groupadd --gid "${USER_GID}" "${USERNAME}"; \
    fi \
    && if getent passwd "${USER_UID}" >/dev/null; then \
        existing_user="$(getent passwd "${USER_UID}" | cut -d: -f1)"; \
        usermod --login "${USERNAME}" --home "/home/${USERNAME}" --move-home \
            --gid "${USER_GID}" --shell /usr/bin/zsh "${existing_user}"; \
    else \
        useradd --uid "${USER_UID}" --gid "${USER_GID}" --create-home \
            --shell /usr/bin/zsh "${USERNAME}"; \
    fi \
    && git config --system --add safe.directory "${IDF_PATH}" \
    && install -d -o "${USERNAME}" -g "${USERNAME}" \
        /workspaces \
        "/home/${USERNAME}/.cache" \
        "${CCACHE_DIR}"

COPY zshrc /etc/skel/.zshrc
RUN cp /etc/skel/.zshrc "/home/${USERNAME}/.zshrc" \
    && chown "${USER_UID}:${USER_GID}" "/home/${USERNAME}/.zshrc"

USER ${USERNAME}
WORKDIR /workspaces/ReflowCtrl

CMD ["zsh"]
