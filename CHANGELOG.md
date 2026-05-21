# Changelog

## [1.8.1](https://github.com/rstreamlabs/rstream-cpp/compare/1.8.0...1.8.1) (2026-05-21)


### Bug Fixes

* let Conan consumers skip SDK binaries ([77c3702](https://github.com/rstreamlabs/rstream-cpp/commit/77c370210d4738af90c035d3867076c7a978f241))
* make C++ runtime tests release-safe ([e128783](https://github.com/rstreamlabs/rstream-cpp/commit/e1287830cabd4f9fb807baec74123422730c4a3f))

## [1.8.0](https://github.com/rstreamlabs/rstream-cpp/compare/1.7.4...1.8.0) (2026-05-21)


### Features

* support PKCS[#11](https://github.com/rstreamlabs/rstream-cpp/issues/11) credential storage ([c7e141c](https://github.com/rstreamlabs/rstream-cpp/commit/c7e141c09a668e2bea08013d6c9cf0bc3fdf9508))


### Bug Fixes

* export libatomic link dependency portably ([4dad5f9](https://github.com/rstreamlabs/rstream-cpp/commit/4dad5f9edae2fa68e3931a96ed4da541d1a36c2a))
* reject unsupported engine proxy config ([ab0e46c](https://github.com/rstreamlabs/rstream-cpp/commit/ab0e46c99cb718c5645c3d8126eefe409fa76620))

## [1.7.4](https://github.com/rstreamlabs/rstream-cpp/compare/1.7.3...1.7.4) (2026-05-17)


### Bug Fixes

* keep disabled geoip header out of aggregate include ([0e2806d](https://github.com/rstreamlabs/rstream-cpp/commit/0e2806d7f3f55da4fb9cc36cf526f229d49cf6a7))

## [1.7.3](https://github.com/rstreamlabs/rstream-cpp/compare/1.7.2...1.7.3) (2026-05-17)


### Bug Fixes

* retry macos code signing timestamp ([f7baba3](https://github.com/rstreamlabs/rstream-cpp/commit/f7baba36ac3d2cc230c0379e7b05c31add8aaba7))

## [1.7.2](https://github.com/rstreamlabs/rstream-cpp/compare/1.7.1...1.7.2) (2026-05-17)


### Bug Fixes

* publish public conan recipe without channel ([50e0a6b](https://github.com/rstreamlabs/rstream-cpp/commit/50e0a6b57a4aebbc0fab1f49658dee7b89735ba4))

## [1.7.1](https://github.com/rstreamlabs/rstream-cpp/compare/1.7.0...1.7.1) (2026-05-17)


### Bug Fixes

* restore cpp release automation ([3227c5f](https://github.com/rstreamlabs/rstream-cpp/commit/3227c5fc9d49d37ce38339d7b04e1bde598aac47))

## [1.7.0](https://github.com/rstreamlabs/rstream-cpp/compare/1.6.0...1.7.0) (2026-05-13)


### Features

* support mTLS agent authentication ([6f55c5a](https://github.com/rstreamlabs/rstream-cpp/commit/6f55c5a93b22e3afcd8f5809a1e6a6f025e97a01))


### Bug Fixes

* clarify mtls agent authentication limits ([aadcec7](https://github.com/rstreamlabs/rstream-cpp/commit/aadcec719896e09a6f69c3f5d6f03f23439192b7))
* harden cpp sdk tunnel runtime ([8eb1b80](https://github.com/rstreamlabs/rstream-cpp/commit/8eb1b8051a0373ca511a1de43d0b34166f787253))

## [1.6.0](https://github.com/rstreamlabs/rstream-cpp/compare/1.5.1...1.6.0) (2026-04-26)


### Features

* add stable domains and upstream TLS ([3d16d26](https://github.com/rstreamlabs/rstream-cpp/commit/3d16d26b46285c5bb1fb4a08bb6502ed4e4d788e))


### Bug Fixes

* keep generated license metadata C++ safe ([a153db9](https://github.com/rstreamlabs/rstream-cpp/commit/a153db9ae9a34bef3ad19db5fbe84d623813fa44))
* regenerate build metadata on reconfigure ([ead0ecc](https://github.com/rstreamlabs/rstream-cpp/commit/ead0eccfa9a326fe14ee558efd794c9c8a05ff2b))

## [1.5.1](https://github.com/rstreamlabs/rstream-cpp/compare/1.5.0...1.5.1) (2026-03-15)


### Bug Fixes

* fix rstream-rtty-server uri parsing ([0ad5560](https://github.com/rstreamlabs/rstream-cpp/commit/0ad5560c4be3eb18cd38cefde3f5fd2f7bdd9928))

## [1.5.0](https://github.com/rstreamlabs/rstream-cpp/compare/1.4.1...1.5.0) (2026-03-15)


### Features

* add webtty client on windows; add windows arm64 build profile ([2c74242](https://github.com/rstreamlabs/rstream-cpp/commit/2c7424218317edf2773ecfa2a34aeb0e627323e6))

## [1.4.1](https://github.com/rstreamlabs/rstream-cpp/compare/1.4.0...1.4.1) (2026-03-08)


### Bug Fixes

* fix windows builds ([b6dcaa4](https://github.com/rstreamlabs/rstream-cpp/commit/b6dcaa47866ddbed16620f0bd39125a7c8c1a21a))

## [1.4.0](https://github.com/rstreamlabs/rstream-cpp/compare/1.3.1...1.4.0) (2026-02-12)


### Features

* rstrm protocol 1.4 (error codes) ([921c488](https://github.com/rstreamlabs/rstream-cpp/commit/921c48817ab44d1b12ce00b8dce5ac5982ff5f21))

## [1.3.1](https://github.com/rstreamlabs/rstream-cpp/compare/1.3.0...1.3.1) (2026-02-11)


### Bug Fixes

* fix client details / labels ([a8a37d3](https://github.com/rstreamlabs/rstream-cpp/commit/a8a37d3cf57f645367c02abca89324fe0be422e3))

## [1.3.0](https://github.com/rstreamlabs/rstream-cpp/compare/1.2.0...1.3.0) (2026-02-11)


### Features

* add webtty labels for service discovery ([818f133](https://github.com/rstreamlabs/rstream-cpp/commit/818f13330657f51cb4f229f51d29923e56238d1b))

## [1.2.0](https://github.com/rstreamlabs/rstream-cpp/compare/1.1.0...1.2.0) (2026-02-11)


### Features

* add ncat utility ([9ba697a](https://github.com/rstreamlabs/rstream-cpp/commit/9ba697ab0961dc7372f0ffd96c896a88dfe687ba))
* rstrm protocol 1.3, netcat, new config model ([4df2cc1](https://github.com/rstreamlabs/rstream-cpp/commit/4df2cc1f6724ed4331aeaa645b80383dd3808e0a))

## [1.1.0](https://github.com/rstreamlabs/rstream-cpp/compare/1.0.0...1.1.0) (2025-08-27)


### Features

* update rtty protocol, adding error messages ([9ad36ab](https://github.com/rstreamlabs/rstream-cpp/commit/9ad36ab1e7d7c670286e0767be84b58d8c7379f1))
* update rtty protocol, adding error messages ([5e70ffa](https://github.com/rstreamlabs/rstream-cpp/commit/5e70ffaa481bb8eb8871fbe1896a26a9dcc05c63))

## 1.0.0 (2025-05-28)


### ⚠ BREAKING CHANGES

* initial commit

### Features

* initial commit ([51335c2](https://github.com/rstreamlabs/rstream-cpp/commit/51335c209177c3053c748787051f2ca4126f2a05))
