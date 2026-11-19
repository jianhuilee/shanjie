# 善解輸入法 shanjie

以「選字正確」為第一優先的開源 macOS 注音輸入法（持續開發中）：每按一個鍵就在本機把整句重新選一次，停頓時再用小模型看前文，從前幾名裡挑最通順的一句。除了檢查更新不連網。

網站：https://shanjie.nyanako.com （介紹、安裝、使用說明、更新紀錄、常見問題）

- 怎麼選字與實測數字：https://shanjie.nyanako.com/how
- 每一版的變化、已合併還沒發版的改動、已知問題：https://shanjie.nyanako.com/updates ；發佈說明在 `docs/releases/`
- 現在的行為與規格：`docs/spec/`
- 授權：程式碼 Apache-2.0（`LICENSE`）；App 內含的 Sparkle（自動更新）是 MIT（`LICENSES/Sparkle-MIT.txt`，含它附帶的第三方授權）；資料見 `LICENSES/data.md`，各來源的整理在 https://shanjie.nyanako.com/licenses
- 想參與開發、回報選錯的字：見 [`CONTRIBUTING.md`](CONTRIBUTING.md)

## 安裝（macOS）

需要 macOS 26（Tahoe）以上、Apple 晶片。下面的方式擇一，不要混用；完整步驟、更新、換安裝方式與解除安裝見 https://shanjie.nyanako.com/install 。

**安裝程式**（推薦）：從 [Releases 的 Latest](https://github.com/Nanako0129/shanjie/releases/latest) 下載 `shanjie-installer-<版本>.zip`，解壓後點兩下「安裝善解輸入法」。歡迎頁可選「這台 Mac」（每個帳號都能用，有程式開著安全輸入時照常能用，安裝與更新各要一次管理員密碼）或「只有我的帳號」（不需要密碼）。

**Homebrew**（只裝在你的帳號）：

```sh
brew install --cask nanako0129/tap/shanjie
"$HOME/Library/Input Methods/善解輸入法.app/Contents/MacOS/shanjie" install
```

第二行在系統還不認得善解時註冊並啟用輸入方式；結束碼是 3 時，到「系統設定 → 鍵盤 → 輸入方式」按「+」，選「繁體中文」裡的「善解輸入法」，按「加入」。

**手動**（只裝在你的帳號）：從 Releases 下載 `shanjie-<版本>.zip`，用 Finder 解壓，在這個 repo 的根目錄執行 `scripts/install-ime.sh <解壓出來的 善解輸入法.app 路徑>`。

裝好後在選單列的輸入法選單選「善解輸入法」；選單裡的「善解設定…」打開設定視窗。v0.4.7 起可以在設定的「關於」頁直接更新。

## 安裝（Linux，fcitx5）

Linux 版是 fcitx5 的輸入法外掛，和 macOS 版用同一個核心、同一份詞庫與模型，全部在本機執行。目前沒有預先建好的套件，要從原始碼建置；沒有自動更新。建置、設定與已知限制的細節見 [`linux/fcitx5/README.md`](linux/fcitx5/README.md)。

**需要**：fcitx5 5.1 以上與它的開發套件、CMake 3.21 以上、支援 C++20 的編譯器、`CONTRIBUTING.md` 寫的 Rust 版本。Debian／Ubuntu：

```sh
sudo apt install fcitx5 fcitx5-config-qt libfcitx5core-dev cmake g++ curl
```

Rust 用 rustup 安裝（https://rustup.rs ）。

**下載模型**（約 250 MB，CC BY-SA 4.0，不在 git 裡）：在 repo 根目錄執行 `CONTRIBUTING.md`「建置與測試」裡的 `curl` 指令（不需要 GitHub 帳號），再檢查雜湊：

```sh
sha256sum -c data/*.sha256
```

**建置與測試**：

```sh
cmake -S linux/fcitx5 -B build/fcitx5 -DCMAKE_BUILD_TYPE=Release -DFCITX_INSTALL_USE_FCITX_SYS_PATHS=ON
cmake --build build/fcitx5 -j
ctest --test-dir build/fcitx5 --output-on-failure
```

**安裝到系統**（每個帳號都能用，要管理員權限）：

```sh
sudo cmake --install build/fcitx5
fcitx5 -rd
```

**只裝在你的帳號**（不需要 sudo）：裝到 `~/.local`，啟動 fcitx5 時告訴它多一個外掛目錄。系統外掛目錄依發行版不同，Debian／Ubuntu 的 x86-64 是 `/usr/lib/x86_64-linux-gnu/fcitx5`。

```sh
cmake -S linux/fcitx5 -B build/fcitx5-user -DCMAKE_BUILD_TYPE=Release \
  -DFCITX_INSTALL_USE_FCITX_SYS_PATHS=OFF -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build/fcitx5-user -j
cmake --install build/fcitx5-user
FCITX_ADDON_DIRS="$HOME/.local/lib/fcitx5:/usr/lib/x86_64-linux-gnu/fcitx5" fcitx5 -rd
```

要讓登入後自動用這個目錄，把 `FCITX_ADDON_DIRS` 加到啟動 fcitx5 的環境裡（例如桌面環境的自動啟動設定）。

**啟用**：打開 fcitx5 設定工具（`fcitx5-configtool`），在「輸入法」把「Shanjie Zhuyin」加到目前的群組。切換用 fcitx5 的觸發鍵（預設 Ctrl+Space）或切換輸入法的熱鍵。設定在輸入法的設定按鈕裡；選字鍵可以從 1–9 改成 `a s d f g h j k l ;`。學習檔在 fcitx5 使用者資料目錄的 `shanjie/learn`。

**注意**：

- fcitx5 對每個視窗分別記住開關狀態；在某個程式裡沒反應時，先在那個視窗按觸發鍵看看。
- 重新啟動 fcitx5 之後，用 snap 裝的 Firefox 不會重新連上，要重開 Firefox。只改設定不必重啟 fcitx5，換新建的外掛才需要。
- 候選窗一律直式；其他與 macOS 版不同的地方見 `linux/fcitx5/README.md` 的「Known limitations」。
