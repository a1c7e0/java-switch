FROM devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
WORKDIR /src
ENV DEVKITPRO=/opt/devkitpro
# Run with a source bind mount. Dependency downloads happen at runtime so the
# managed environment's CA bundle can be mounted without storing it in an image.
CMD ["bash", "scripts/build-all.sh"]
