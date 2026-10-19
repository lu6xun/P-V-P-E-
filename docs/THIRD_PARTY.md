# 第三方依赖与许可证

CMake 固定下载以下公开源码，项目没有复制第三方实现到自己的源码目录。离线下载缓存在 `downloads/`（被 gitignore 排除）。发布可执行文件时，请附带下述上游许可证文本。

| 依赖 | 版本 / 来源 | 许可证 |
| --- | --- | --- |
| raylib | [5.5](https://github.com/raysan5/raylib/tree/5.5) | zlib/libpng，源码 `LICENSE`；其内置第三方组件见 `src/external` 及各源码头 |
| GLFW（由 raylib 内置） | 随 raylib 5.5 固定 | zlib/libpng，源码 `src/external/glfw/LICENSE.md` |
| Asio | [asio-1-30-2](https://github.com/chriskohlhoff/asio/tree/asio-1-30-2) | Boost Software License 1.0，`asio/LICENSE_1_0.txt` |
| nlohmann/json | [v3.11.3](https://github.com/nlohmann/json/tree/v3.11.3) | MIT，`LICENSE.MIT` |

具体版本、下载 URL 与归档哈希的权威记录在根目录 `CMakeLists.txt`。
