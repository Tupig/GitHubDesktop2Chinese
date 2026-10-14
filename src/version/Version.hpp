#ifndef VERSIONPARSE_VERSION_HPP
#define VERSIONPARSE_VERSION_HPP


#pragma once
#include <string>

namespace versionparse {

class Version {
public:
    enum Status {
        Dev,            /*开发版*/
        Beta,           /*测试版*/
        Release         /*正式发布版*/
    };
    int major = 0;
    int minor = 0;
    int revision = 0;
    int betaversion = 0;
    Status status = Status::Release;

private:
    enum ParseCheckPosition {
        enummajor,          /*反序列化标志位 主版本号*/
        enumminor,          /*反序列化标志位 次版本号*/
        enumrevision,       /*反序列化标志位 修订号*/
        enumbetaversion,    /*反序列化标志位 测试版本号*/
    };
    ParseCheckPosition parseCheckPos = ParseCheckPosition::enummajor;
    bool valid = false;
public:
	Version(const char* v) {
        valid = Parse(v);
	}

    Version(int major, int minor, int revision, Status status = Status::Release, int betaversion = 0)
    : major(major), minor(minor), revision(revision), status(status), betaversion(betaversion)
    {
        valid = true;
    }

public:
    bool operator==(const Version& b) const {
        if (major != b.major) return false;
        if (minor != b.minor) return false;
        if (revision != b.revision) return false;
        // 如果双方都为dev 或 beta版 则进行内部版本号的比较
        if (this->status == b.status) {
            if(this->status == Status::Release) {
                return true;
            }
            else {
                return betaversion == b.betaversion;
            }
        }
        return false;
    }

    bool operator<=(const Version& b) const {
        return *this < b || *this == b;
    }

    // 正式版 1.13.1 大于 非正式版 1.13.1.beta.1
    bool operator<(const Version& b) const {
        if(major < b.major) return true;
        else if(major > b.major) return false;

        if(minor < b.minor) return true;
        else if(minor > b.minor) return false;

        if(revision < b.revision) return true;
        else if(revision > b.revision) return false;
        // 到这里表示主版本号相同
        // 如果两份的公开版本类型相同
        if(this->status == b.status) {
            if(this->status == Status::Release) {// 如果都是发布版, 应该相等
                return false;
            }
            else {
                return betaversion < b.betaversion;
            }
        }
        else { // 公开版本类型不同
            if(status == Status::Release) {
                // 如果我是正式版, 则我大于它
                return false;
            }
            else if(b.status == Status::Release){
                // 如果对方是正式版, 则我肯定小于它
                return true;
            }
            else if(status == Status::Beta) {
                // 如果我是测试版, 对方只能是 开发版
                // 则我大于他
                return false;
            }
            else {
                // 我是开发板 对方测试版
                return true;
            }
        }
    }

    bool operator>=(const Version& b) const {
        return b <= *this;
    }

    bool operator>(const Version& b) const {
        return b < *this;
    }

    operator bool() const {
        return valid;
    }

public:
    /**
     * @brief 转为字符串形式方便阅读
     * @param hasStatus 是否显示版本类型
     * @param hasBetaVer 是否显示测试版本号
     * @return 返回格式为 0.0.0 或 0.0.0 - Beta 或 0.0.0.0 - Beta
    */
    std::string toString(bool hasStatus = false, bool hasBetaVer = false) {
        std::string ret = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(revision);
        if (hasBetaVer) {
            ret += "." + std::to_string(betaversion);
        }
        if (hasStatus) {
            if (status == Status::Dev)  ret += " - Dev";
            if (status == Status::Beta)  ret += " - Beta";
            if (status == Status::Release)  ret += " - Release";
        }
        return ret;
    }

private:
    /**
     * @brief 将A~Z的字符转为小写,其他字符原样返回
     * @param c 字符 A~Z
     * @return 返回小写的a~z
    */
    char ToLower(char c) {
        if (c >= 65 && c <= 90) {  // A ~ Z
            return c + 32;
        }
        return c;
    }

    /**
     * @brief 判断字符是否为数字 0~9
     */
    static bool IsDigit(char c) {
        return c >= '0' && c <= '9';
    }

    // 版本号单个组件允许的最大值, 超出视为非法输入(防止数字串过长导致整型溢出 UB)
    static constexpr int MAX_VERSION_COMPONENT = 99999;

    /**
     * @brief 检查版本是否是alpha beta
     * 
     * @param sourcevalue
     * @param checkvalue            判断字符是否和这个相等 必须小写 这个数不能是空字符串 因为没有意义
     * @return                      <0: 遇到了 \0, 0: 不是, >0 是
    */
    int CheckIsAlphaOrBeta(const char* sourcevalue, const char* checkvalue) {
        int _increase = 1;          // 如果返回值大于0 则返回这个数,来告诉调用者应该将原始字节偏移多少才能跳过这个检查值
        for (const char* _v = sourcevalue; *_v != '\0'; _v++)
        {
            if (ToLower(*_v) != *checkvalue) {
                return 0;
            }
            if (*(checkvalue + 1) == '\0') {
                return _increase;
            }
            checkvalue++;
            _increase++;
        }
        return -1;
    }

    // '-'/'_' 后的预发布后缀解析(单一实现, #121): alpha/dev -> Dev, beta -> Beta
    // 入参 v 已跳过 '-'/'_'; 匹配则推进 v(含可选的 '.')、写入 status 并返回 true(调用方置 enumbetaversion 后 continue);
    // 未知/不完整后缀返回 false, 调用方结束解析并保留已识别部分(防止后缀数字被并入版本号, #120)
    bool TryParsePreReleaseSuffix(const char*& v) {
        // 检查是否是 alpha 或者 dev
        // (suffix_len: 跳过检查值的字节偏移; 命名区分外层的解析结果 bool ret, #122 E6)
        int suffix_len = CheckIsAlphaOrBeta(v, "alpha");
        if(!suffix_len) suffix_len = CheckIsAlphaOrBeta(v, "dev");
        if (suffix_len < 0) return false;
        if (suffix_len > 0) { // 是 alpha 内部测试版 -> Dev
            status = Status::Dev;
        }
        else {
            // 检查是否是 beta
            suffix_len = CheckIsAlphaOrBeta(v, "beta");
            if (suffix_len <= 0) return false;
            status = Status::Beta;
        }
        v += suffix_len;
        if (*v == '.') {
            v++;
        }
        return true;
    }

    /**
     * @brief 反序列化 将字符串反序列化为内部参数
     * @param v 
     * @return 是否读出了版本号, 至少要读出两位版本号(1.2)
    */
    bool Parse(const char* v) {
        bool ret = false;
        bool overflow = false;  // 数字串超长(超过 MAX_VERSION_COMPONENT)时置位
        for (;;) {
            if (*v == '\0') {
                break;
            }
            // parseCheckPos == ParseCheckPosition::enummajor
            if (parseCheckPos == ParseCheckPosition::enummajor) {
                if (*v == 'v' || *v == 'V') {
                    v++;
                    continue;
                }
                if (*v == '.') {
                    parseCheckPos = ParseCheckPosition::enumminor;
                    v++;
                    continue;
                }
                if (IsDigit(*v)) {
                    major = major * 10 + (*v - '0');
                    if (major > MAX_VERSION_COMPONENT) { overflow = true; break; }
                    v++;
                    continue;
                }
                // 到这里不可能是. \\n 数字
                // 所以只能是非法字符(为了兼容性,将空格放过)
                if (*v == ' ') {
                    v++;
                    continue;
                }
                // 非法字符 反序列化失败
                break;
            }
            // parseCheckPos == ParseCheckPosition::minor
            if (parseCheckPos == ParseCheckPosition::enumminor) {
                if (*v == '.') {
                    parseCheckPos = ParseCheckPosition::enumrevision;
                    v++;
                    continue;
                }
                if (IsDigit(*v)) {
                    minor = minor * 10 + (*v - '0');
                    if (minor > MAX_VERSION_COMPONENT) { overflow = true; break; }
                    v++;
                    ret = true;
                    continue;
                }
                if (*v == '-' || *v == '_') {
                    v++;
                    // alpha/dev/beta 预发布后缀: 单一实现(双份拷贝曾致同类解析 bug 存在两处, #121)
                    if (TryParsePreReleaseSuffix(v)) {
                        parseCheckPos = ParseCheckPosition::enumbetaversion;
                        continue;
                    }
                    // 未知后缀(如 -rc1): 结束解析并保留已识别部分,
                    // 防止后缀中的数字被并入版本号(如 "1.2-rc1" 被解析为 1.21, #120)
                    break;
                }
                v++;
                continue;
            }
            // parseCheckPos == ParseCheckPosition::enumrevision
            if (parseCheckPos == ParseCheckPosition::enumrevision) {
                if (*v == '.') {
                    parseCheckPos = ParseCheckPosition::enumbetaversion;
                    v++;
                    continue;
                }
                if (IsDigit(*v)) {
                    revision = revision * 10 + (*v - '0');
                    if (revision > MAX_VERSION_COMPONENT) { overflow = true; break; }
                    v++;
                    continue;
                }
                if (*v == '-' || *v == '_') {
                    v++;
                    // alpha/dev/beta 预发布后缀: 单一实现(双份拷贝曾致同类解析 bug 存在两处, #121)
                    if (TryParsePreReleaseSuffix(v)) {
                        parseCheckPos = ParseCheckPosition::enumbetaversion;
                        continue;
                    }
                    // 未知后缀(如 -4/-rc1): 结束解析并保留已识别部分,
                    // 防止后缀中的数字被并入版本号(如 "1.2.3-4" 中的 4 被静默丢弃, #120)
                    break;
                }
                v++;
                continue;
            }
            // parseCheckPos == ParseCheckPosition::enumbetaversion
            if (parseCheckPos == ParseCheckPosition::enumbetaversion) {
                // 这里遇到'.'就结束了, 版本字符串的结构只能支持到 0.0.0.0
                if (*v == '.') {
                    break;
                }
                if (IsDigit(*v)) {
                    if (status == Status::Release && ((*v - '0') != 0)) status = Status::Beta;
                    betaversion = betaversion * 10 + (*v - '0');
                    if (betaversion > MAX_VERSION_COMPONENT) { overflow = true; break; }
                    v++;
                    continue;
                }

                v++;
                continue;
            }
        }
        return ret && !overflow;
    }
};

}


#endif // !VERSIONPARSE_VERSION_HPP
