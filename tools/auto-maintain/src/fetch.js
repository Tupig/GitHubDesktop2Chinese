import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { pipeline } from 'node:stream/promises';
import { Readable } from 'node:stream';

const GITHUB_DESKTOP_REPO = 'desktop/desktop';
const API_BASE = 'https://api.github.com';
// Windows 版 Squirrel 完整包（本质为 zip，内含 lib/net45/resources/app/{main,renderer}.js）。
// 注意：不能使用 macOS 的 GitHub.Desktop-x64.zip —— 两个平台的应用文案不同
// （Windows 菜单带 & 访问键、路径相关文案为 Explorer/Command Prompt 等），
// 使用 macOS 包会产生大量假失效。
const ASSET_PATTERN = /^GitHubDesktop-[\d.]+-x64-full\.nupkg$/;

// 单次 HTTP 请求超时: 查询 45s, 大文件下载 15min(慢网络兜底, 配合断点续传)
const QUERY_TIMEOUT_MS = 45_000;
const DOWNLOAD_TIMEOUT_MS = 900_000;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/**
 * 获取最新 GitHub Desktop 的 release 信息
 * 返回 { tag, version, zipAssetUrl }
 * 网络抖动/限流时内置重试(共2次), 单次请求带超时避免挂起
 */
export async function getLatestRelease() {
  const headers = { 'User-Agent': 'githubdesktop2chinese-auto-maintain' };
  // CI 中带上 token 规避 GitHub API 匿名限流(60次/小时, runner 共享IP极易触发403); 本地无 token 则匿名访问
  if (process.env.GITHUB_TOKEN) {
    headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;
  }

  const attempts = 2;
  let lastError;
  for (let attempt = 1; attempt <= attempts; attempt++) {
    try {
      const res = await fetch(`${API_BASE}/repos/${GITHUB_DESKTOP_REPO}/releases/latest`, {
        headers,
        signal: AbortSignal.timeout(QUERY_TIMEOUT_MS),
      });
      if (!res.ok) {
        const err = new Error(`获取 GitHub Desktop 最新 release 失败: HTTP ${res.status}`);
        // #132: 限流(403/429)时遵守 Retry-After(上限 90s), 固定 5s 重试在限流场景必败
        const retryAfter = Number(res.headers.get('retry-after'));
        err.waitMs = Number.isFinite(retryAfter) && retryAfter > 0 ? Math.min(retryAfter, 90) * 1000 : undefined;
        throw err;
      }
      const data = await res.json();
      const tag = data.tag_name; // 形如 release-3.6.6
      const version = tag.replace(/^release-/, '');
      const assets = Array.isArray(data.assets) ? data.assets : [];
      const asset = assets.find((a) => ASSET_PATTERN.test(a.name));
      if (!asset) {
        throw new Error(`未在 release ${tag} 中找到 Windows 完整包 ${ASSET_PATTERN}`);
      }
      return { tag, version, assetName: asset.name, zipUrl: asset.browser_download_url, zipSize: asset.size, zipDigest: asset.digest ?? null };
    } catch (e) {
      lastError = e;
      if (attempt < attempts) {
        const waitMs = e.waitMs ?? 5_000;
        console.warn(`获取 release 信息失败(第 ${attempt}/${attempts} 次): ${e.message}，${Math.round(waitMs / 1000)} 秒后重试`);
        await sleep(waitMs);
      }
    }
  }
  throw new Error(`获取 GitHub Desktop 最新 release 连续失败 ${attempts} 次: ${lastError?.message ?? '未知错误'}`);
}

// ZIP 规范: EOCD 结构固定 22 字节, 归档注释(archive comment)最大 65535 字节
const EOCD_MIN_SIZE = 22;
const MAX_ARCHIVE_COMMENT = 65535;
// EOCD 搜索窗口 = EOCD 结构 + 最大注释(归档带注释时 EOCD 会整体前移)
const EOCD_SEARCH_WINDOW = EOCD_MIN_SIZE + MAX_ARCHIVE_COMMENT;

/**
 * 快速校验 zip/nupkg 完整性：检查 EOCD 签名 (PK\x05\x06) 是否出现在文件末尾。
 */
export function isZipComplete(zipPath) {
  try {
    const fd = fs.openSync(zipPath, 'r');
    try {
      const size = fs.fstatSync(fd).size;
      if (size < EOCD_MIN_SIZE) return false;
      // EOCD(签名 PK\x05\x06)位于归档尾部; 归档带 archive comment 时会前移,
      // 因此在尾部窗口(EOCD + 最大注释长度)内向前搜索
      const windowLen = Math.min(size, EOCD_SEARCH_WINDOW);
      const buf = Buffer.alloc(windowLen);
      fs.readSync(fd, buf, 0, windowLen, size - windowLen);
      const sig = Buffer.from([0x50, 0x4b, 0x05, 0x06]);
      return buf.lastIndexOf(sig) !== -1;
    } finally {
      fs.closeSync(fd);
    }
  } catch {
    return false;
  }
}

/**
 * 计算文件 sha256（流式），返回 hex 小写
 */
export function sha256File(filePath) {
  return new Promise((resolve, reject) => {
    const hash = createHash('sha256');
    const stream = fs.createReadStream(filePath);
    stream.on('error', reject);
    stream.on('data', (chunk) => hash.update(chunk));
    stream.on('end', () => resolve(hash.digest('hex')));
  });
}

/**
 * 校验文件内容是否与 release 资产 digest（形如 "sha256:hex"）一致（#118 下载完整性）。
 * digest 缺失或格式异常时视为通过（告警，维持 size+EOCD 基线）；不匹配返回 true。
 */
export async function digestMismatch(filePath, expectedDigest) {
  if (!expectedDigest) return false;
  const [algo, expected] = expectedDigest.split(':');
  if (algo !== 'sha256' || !/^[0-9a-f]{64}$/.test(expected ?? '')) {
    console.warn(`release 资产 digest 字段格式异常: ${expectedDigest}，跳过内容哈希校验`);
    return false;
  }
  const got = await sha256File(filePath);
  if (got !== expected) {
    console.warn(`内容哈希不符: 实际 sha256=${got}，资产 digest=${expected}`);
    return true;
  }
  return false;
}

/**
 * 下载 release nupkg（支持断点续传），返回文件路径
 * expectedSize 为 release 资产声明的字节数(可选): 用于复用校验与下载后尺寸核对
 * expectedDigest 为 release 资产 digest 字段(可选, "sha256:hex"): 内容哈希校验, 弥补 EOCD 无法检出中段损坏的缺口(#118)
 */
export async function downloadZip(url, destDir, expectedSize, expectedDigest) {
  fs.mkdirSync(destDir, { recursive: true });
  const zipPath = path.join(destDir, 'github-desktop.nupkg');
  const tmpPath = zipPath + '.part';
  const tmpUrlPath = tmpPath + '.url'; // 旁证: .part 来自哪个 URL, 防止跨版本续传拼出混合文件

  // 已下载文件: 尺寸与本次 release 资产一致、EOCD 完整且内容哈希一致才复用(版本变化后的旧缓存一律重下)
  if (fs.existsSync(zipPath)) {
    const sizeMatches = !expectedSize || fs.statSync(zipPath).size === expectedSize;
    if (sizeMatches && isZipComplete(zipPath) && !(await digestMismatch(zipPath, expectedDigest))) {
      return zipPath;
    }
    fs.rmSync(zipPath, { force: true });
  }

  // 断点来源旁证: .part 属于其它 URL(GitHub Desktop 已更新版本)或无旁证(旧版残留,
  // 无法证明来源一致)时, 续传会把两个版本拼在一起, 必须删掉从头下载
  if (fs.existsSync(tmpPath)) {
    let prevUrl = '';
    try {
      prevUrl = fs.readFileSync(tmpUrlPath, 'utf8').trim();
    } catch { /* 无旁证按不一致处理 */ }
    if (prevUrl !== url) {
      fs.rmSync(tmpPath, { force: true });
      fs.rmSync(tmpUrlPath, { force: true });
    }
  }

  // 支持 Range 续传
  const headers = { 'User-Agent': 'githubdesktop2chinese-auto-maintain' };
  let hasPartial = false;
  let partialSize = 0;
  if (fs.existsSync(tmpPath)) {
    const size = fs.statSync(tmpPath).size;
    if (size > 0) {
      headers.Range = `bytes=${size}-`;
      hasPartial = true;
      partialSize = size;
    }
  }
  const res = await fetch(url, { headers, signal: AbortSignal.timeout(DOWNLOAD_TIMEOUT_MS) });

  // 服务器返回的断点位置与请求不符(部分代理/CDN 会改写 Range): 追加写入必然损坏文件,
  // 重置断点后抛出, 由上层重试从头下载
  if (res.status === 206 && hasPartial) {
    const cr = res.headers.get('content-range'); // 形如 "bytes 1234-5678/9012"
    const m = cr && /^bytes (\d+)-/.exec(cr);
    if (m && Number(m[1]) !== partialSize) {
      fs.rmSync(tmpPath, { force: true });
      throw new Error(`服务器返回的断点位置与请求不符(Content-Range: ${cr}), 已重置断点文件, 重试将从头下载`);
    }
  }
  // 416: 断点尺寸已达到服务器认为的完整大小, 但本地并非完整 zip(极端残留)。
  // 必须删除断点, 否则每次运行都会因同一断点反复 416 卡死, 永远无法自愈
  if (res.status === 416 && hasPartial) {
    fs.rmSync(tmpPath, { force: true });
    throw new Error('断点文件与服务器不匹配(HTTP 416), 已删除断点文件, 重试将从头下载');
  }
  if (res.status !== 200 && res.status !== 206) {
    throw new Error(`下载 GitHub Desktop 失败: HTTP ${res.status}`);
  }

  // 服务器不支持 Range 时（200），若已有部分文件则必须从头覆盖，避免追加损坏
  const isPartial = hasPartial && res.status === 206;
  fs.writeFileSync(tmpUrlPath, url); // 写入旁证: 此后 .part 均由该 URL 产生
  const file = fs.createWriteStream(tmpPath, { flags: isPartial ? 'a' : 'w' });
  await pipeline(Readable.fromWeb(res.body), file);
  if (!isZipComplete(tmpPath)) {
    // 不完整（缺少 EOCD 记录）: 保留 .part 断点文件以便下次续传, 直接失败, 绝不改名覆盖
    throw new Error('下载的 nupkg 不完整（缺少 EOCD 记录）, 已保留断点文件供续传');
  }
  const gotSize = fs.statSync(tmpPath).size;
  if (expectedSize && gotSize !== expectedSize) {
    // EOCD 存在但尺寸与资产声明不符(代理注入/半截拼接/陈旧缓存): 不入库, 删断点从头重下
    fs.rmSync(tmpPath, { force: true });
    fs.rmSync(tmpUrlPath, { force: true });
    throw new Error(`下载尺寸(${gotSize})与 release 资产(${expectedSize})不符, 已删除断点文件, 重试将从头下载`);
  }
  // 内容哈希校验(#118): EOCD 在文件尾部, 中段损坏/注入无法由尺寸与 EOCD 检出, 须按资产 digest 复核
  if (await digestMismatch(tmpPath, expectedDigest)) {
    fs.rmSync(tmpPath, { force: true });
    fs.rmSync(tmpUrlPath, { force: true });
    throw new Error(`下载内容 sha256 与 release 资产 digest 不符, 已删除断点文件, 重试将从头下载`);
  }
  fs.renameSync(tmpPath, zipPath);
  fs.rmSync(tmpUrlPath, { force: true });
  return zipPath;
}

/**
 * 解压 nupkg，提取 main.js 和 renderer.js
 * nupkg 内含 "lib/net45/resources/app/{main,renderer}.js"
 * versionTag: 上游版本号, 用于提取结果复用标记(缺省回退文件名, 防同尺寸跨版本误复用)
 * 返回 { mainJsPath, rendererJsPath, appDir }
 */
export function extractJs(zipPath, workDir, versionTag = '') {
  const extractDir = path.join(workDir, 'extracted');
  // 提取结果与来源包一致时直接复用（配合 CI 缓存，避免每次重复解压 150MB+ 安装包）；
  // 标记含版本号与文件尺寸(旧格式 "文件:尺寸" 因格式变化自然失效重建)
  const sourceId = `${versionTag || path.basename(zipPath)}:${fs.statSync(zipPath).size}`;
  const markerPath = path.join(extractDir, '.source.txt');
  if (fs.existsSync(markerPath) && fs.readFileSync(markerPath, 'utf8').trim() === sourceId) {
    const cached = findAppDir(extractDir);
    if (cached) {
      const cachedMain = path.join(cached, 'main.js');
      const cachedRenderer = path.join(cached, 'renderer.js');
      if (fs.existsSync(cachedMain) && fs.existsSync(cachedRenderer)) {
        return { mainJsPath: cachedMain, rendererJsPath: cachedRenderer, appDir: cached };
      }
    }
  }

  fs.rmSync(extractDir, { recursive: true, force: true });
  fs.mkdirSync(extractDir, { recursive: true });

  // 优先用系统 unzip / tar，跨平台。
  // execFileSync 数组参数: 路径不经过 shell 解释, 消除 zipPath/extractDir 的注入面
  const unzip = tryExec('unzip', ['-q', '-o', zipPath, '-d', extractDir]);
  if (!unzip) {
    const tar = tryExec('tar', ['-xf', zipPath, '-C', extractDir]);
    if (!tar) {
      throw new Error(`解压失败: unzip 与 tar 均不可用或解压出错 (${zipPath})`);
    }
  }

  const appDir = findAppDir(extractDir);
  if (!appDir) {
    throw new Error('解压后未找到 app 资源目录');
  }
  const mainJsPath = path.join(appDir, 'main.js');
  const rendererJsPath = path.join(appDir, 'renderer.js');
  if (!fs.existsSync(mainJsPath) || !fs.existsSync(rendererJsPath)) {
    throw new Error(`app 目录中缺少 main.js / renderer.js: ${appDir}`);
  }
  fs.writeFileSync(markerPath, sourceId);
  return { mainJsPath, rendererJsPath, appDir };
}

function tryExec(cmd, args) {
  try {
    execFileSync(cmd, args, { stdio: 'pipe' });
    return true;
  } catch {
    return false;
  }
}

function findAppDir(root) {
  const SKIP = new Set(['node_modules', 'Frameworks', 'MacOS', 'copilot', 'git', 'static']);
  // 显式栈迭代(避免极深目录树递归栈深风险); 目标 app 目录唯一, 遍历顺序不影响结果
  const stack = [root];
  while (stack.length > 0) {
    const dir = stack.pop();
    const children = [];
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      if (!entry.isDirectory()) continue;
      if (SKIP.has(entry.name)) continue;
      const full = path.join(dir, entry.name);
      if (entry.name === 'app' && fs.existsSync(path.join(full, 'main.js')) && fs.existsSync(path.join(full, 'renderer.js'))) {
        return full;
      }
      children.push(full);
    }
    for (let i = children.length - 1; i >= 0; i--) {
      stack.push(children[i]);
    }
  }
  return null;
}

/**
 * 一键流程：获取最新版 -> 下载 -> 解压 -> 提取 JS
 */
export async function fetchLatest(workDir) {
  const release = await getLatestRelease();
  const zipPath = await downloadZip(release.zipUrl, workDir, release.zipSize, release.zipDigest);
  const js = extractJs(zipPath, workDir, release.version);
  return { ...release, ...js };
}
