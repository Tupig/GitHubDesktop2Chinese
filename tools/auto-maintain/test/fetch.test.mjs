// fetch.js 单元测试（Vitest）
// 覆盖 getLatestRelease 网络分支（stub 全局 fetch，不真实联网）与 downloadZip 的
// 缓存复用/续传/416 自愈/Content-Range 不符/尺寸与哈希校验，以及 extractJs 缓存复用分支。
import { describe, it, expect, vi, afterEach, beforeEach } from 'vitest';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {
  getLatestRelease, downloadZip, extractJs, extractSourceId, buildLatestReleaseUrl,
} from '../src/fetch.js';

/** 构造一个 EOCD 完整的假 zip（尾部含 PK\x05\x06） */
function fakeZipBytes(padding = 30) {
  return Buffer.concat([Buffer.alloc(padding, 1), Buffer.from([0x50, 0x4b, 0x05, 0x06])]);
}

let workDir;
beforeEach(() => {
  workDir = fs.mkdtempSync(path.join(os.tmpdir(), 'ghd2c-fetch-'));
});
afterEach(() => {
  vi.unstubAllGlobals();
  vi.useRealTimers();
  fs.rmSync(workDir, { recursive: true, force: true });
});

describe('getLatestRelease：release 查询与校验', () => {
  it('成功：解析 tag/版本/资产并带默认 User-Agent', async () => {
    const fetchMock = vi.fn(async () => new Response(JSON.stringify({
      tag_name: 'release-3.6.6',
      assets: [
        { name: 'GitHub.Desktop-x64.zip' },
        { name: 'GitHubDesktop-3.6.6-x64-full.nupkg', browser_download_url: 'https://example.invalid/a.nupkg', size: 123, digest: 'sha256:abc' },
      ],
    }), { status: 200, headers: { 'content-type': 'application/json' } }));
    vi.stubGlobal('fetch', fetchMock);

    const rel = await getLatestRelease();
    expect(rel).toMatchObject({
      tag: 'release-3.6.6',
      version: '3.6.6',
      assetName: 'GitHubDesktop-3.6.6-x64-full.nupkg',
      zipUrl: 'https://example.invalid/a.nupkg',
      zipSize: 123,
      zipDigest: 'sha256:abc',
    });
    expect(fetchMock).toHaveBeenCalledWith(buildLatestReleaseUrl(), expect.objectContaining({
      headers: expect.objectContaining({ 'User-Agent': 'githubdesktop2chinese-auto-maintain' }),
    }));
  });

  it('GITHUB_TOKEN 存在时附带 Authorization 头', async () => {
    const fetchMock = vi.fn(async () => new Response(JSON.stringify({
      tag_name: '3.6.6',
      assets: [{ name: 'GitHubDesktop-3.6.6-x64-full.nupkg', browser_download_url: 'u', size: 1 }],
    }), { status: 200 }));
    vi.stubGlobal('fetch', fetchMock);
    process.env.GITHUB_TOKEN = 'tok-123';
    try {
      await getLatestRelease();
    } finally {
      delete process.env.GITHUB_TOKEN;
    }
    expect(fetchMock.mock.calls[0][1].headers.Authorization).toBe('Bearer tok-123');
  });

  it('HTTP 失败后重试，连续失败抛错（假定时器加速退避）', async () => {
    vi.useFakeTimers();
    const fetchMock = vi.fn(async () => new Response('nope', { status: 500, headers: { 'retry-after': '1' } }));
    vi.stubGlobal('fetch', fetchMock);

    const pending = getLatestRelease();
    const assertion = expect(pending).rejects.toThrow(/连续失败/);
    await vi.runAllTimersAsync();
    await assertion;
    expect(fetchMock).toHaveBeenCalledTimes(2);
  });
});

describe('downloadZip：下载、续传与完整性校验', () => {
  it('全新下载：写入 zipPath 并清理 .part/.url 旁证', async () => {
    const bytes = fakeZipBytes();
    const fetchMock = vi.fn(async () => new Response(bytes, { status: 200 }));
    vi.stubGlobal('fetch', fetchMock);

    const zipPath = await downloadZip('https://example.invalid/a.nupkg', workDir, bytes.length, null);
    expect(fs.existsSync(zipPath)).toBe(true);
    expect(fs.readFileSync(zipPath).length).toBe(bytes.length);
    expect(fs.existsSync(zipPath + '.part')).toBe(false);
    expect(fs.existsSync(zipPath + '.part.url')).toBe(false);
  });

  it('缓存复用：尺寸/完整/EOCD/digest 均一致时不发起网络请求', async () => {
    const bytes = fakeZipBytes();
    const zipPath = path.join(workDir, 'github-desktop.nupkg');
    fs.writeFileSync(zipPath, bytes);
    const fetchMock = vi.fn(async () => { throw new Error('不该被调用'); });
    vi.stubGlobal('fetch', fetchMock);

    const got = await downloadZip('https://example.invalid/a.nupkg', workDir, bytes.length, null);
    expect(got).toBe(zipPath);
    expect(fetchMock).not.toHaveBeenCalled();
  });

  it('尺寸不符的陈旧缓存被丢弃并重下', async () => {
    const bytes = fakeZipBytes();
    const zipPath = path.join(workDir, 'github-desktop.nupkg');
    fs.writeFileSync(zipPath, fakeZipBytes(999)); // 尺寸不符
    const fetchMock = vi.fn(async () => new Response(bytes, { status: 200 }));
    vi.stubGlobal('fetch', fetchMock);

    await downloadZip('https://example.invalid/a.nupkg', workDir, bytes.length, null);
    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fs.readFileSync(zipPath).length).toBe(bytes.length);
  });

  it('.part 旁证 URL 不同 → 丢弃断点从头下载', async () => {
    const bytes = fakeZipBytes();
    const zipPath = path.join(workDir, 'github-desktop.nupkg');
    fs.writeFileSync(zipPath + '.part', Buffer.alloc(5, 9));
    fs.writeFileSync(zipPath + '.part.url', 'https://example.invalid/OLD.nupkg');
    const fetchMock = vi.fn(async () => new Response(bytes, { status: 200 }));
    vi.stubGlobal('fetch', fetchMock);

    await downloadZip('https://example.invalid/NEW.nupkg', workDir, bytes.length, null);
    expect(fetchMock.mock.calls[0][1].headers.Range).toBeUndefined(); // 未携带续传 Range
    expect(fs.readFileSync(zipPath).length).toBe(bytes.length);
  });

  it('206 但 Content-Range 起始与请求不符 → 重置断点并抛错', async () => {
    const zipPath = path.join(workDir, 'github-desktop.nupkg');
    fs.writeFileSync(zipPath + '.part', Buffer.alloc(5, 9));
    fs.writeFileSync(zipPath + '.part.url', 'https://example.invalid/a.nupkg');
    vi.stubGlobal('fetch', vi.fn(async () => new Response(fakeZipBytes(), {
      status: 206, headers: { 'content-range': 'bytes 999-1032/1033' },
    })));

    await expect(downloadZip('https://example.invalid/a.nupkg', workDir, null, null))
      .rejects.toThrow(/断点位置与请求不符/);
    expect(fs.existsSync(zipPath + '.part')).toBe(false);
  });

  it('416 且存在断点 → 自愈删除断点并抛错（避免每次卡死）', async () => {
    const zipPath = path.join(workDir, 'github-desktop.nupkg');
    fs.writeFileSync(zipPath + '.part', Buffer.alloc(5, 9));
    fs.writeFileSync(zipPath + '.part.url', 'https://example.invalid/a.nupkg');
    vi.stubGlobal('fetch', vi.fn(async () => new Response('', { status: 416 })));

    await expect(downloadZip('https://example.invalid/a.nupkg', workDir, null, null))
      .rejects.toThrow(/416/);
    expect(fs.existsSync(zipPath + '.part')).toBe(false);
  });

  it('下载内容缺少 EOCD → 保留断点并抛错（不改名覆盖）', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => new Response(Buffer.alloc(30, 1), { status: 200 })));
    await expect(downloadZip('https://example.invalid/a.nupkg', workDir, null, null))
      .rejects.toThrow(/不完整/);
    expect(fs.existsSync(path.join(workDir, 'github-desktop.nupkg'))).toBe(false);
  });

  it('尺寸与资产声明不符 → 删除断点并抛错', async () => {
    const bytes = fakeZipBytes();
    vi.stubGlobal('fetch', vi.fn(async () => new Response(bytes, { status: 200 })));
    await expect(downloadZip('https://example.invalid/a.nupkg', workDir, bytes.length + 1, null))
      .rejects.toThrow(/尺寸/);
  });

  it('digest 不符 → 删除断点并抛错', async () => {
    const bytes = fakeZipBytes();
    vi.stubGlobal('fetch', vi.fn(async () => new Response(bytes, { status: 200 })));
    const warn = console.warn;
    console.warn = () => {};
    try {
      await expect(downloadZip('https://example.invalid/a.nupkg', workDir, bytes.length, `sha256:${'0'.repeat(64)}`))
        .rejects.toThrow(/digest/);
    } finally {
      console.warn = warn;
    }
  });
});

describe('extractJs：提取结果缓存复用', () => {
  it('标记与来源一致且存在 main.js/renderer.js → 直接复用，不再解压', () => {
    const zipPath = path.join(workDir, 'github-desktop.nupkg');
    fs.writeFileSync(zipPath, fakeZipBytes());
    const size = fs.statSync(zipPath).size;
    const extractDir = path.join(workDir, 'extracted');
    const appDir = path.join(extractDir, 'lib', 'net45', 'resources', 'app');
    fs.mkdirSync(appDir, { recursive: true });
    fs.writeFileSync(path.join(appDir, 'main.js'), '// main');
    fs.writeFileSync(path.join(appDir, 'renderer.js'), '// renderer');
    fs.writeFileSync(path.join(extractDir, '.source.txt'),
      extractSourceId('3.6.6', 'github-desktop.nupkg', size));

    const r = extractJs(zipPath, workDir, '3.6.6');
    expect(r.appDir).toBe(appDir);
    expect(fs.existsSync(r.mainJsPath)).toBe(true);
    expect(fs.existsSync(r.rendererJsPath)).toBe(true);
  });
});
