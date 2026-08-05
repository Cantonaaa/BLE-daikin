#pragma once
#include <stddef.h>

/*
 * 将 UTF-8 名称转换为拼音 key（带声调，空格分隔）
 * 用于语音命令分组：同音/同字的分机归为一组。
 * 中文字符与阿拉伯数字都会转拼音（如 "2" -> "èr"）。
 *
 * 返回值：成功写入的字节数；若名称包含英文/标点等其他字符，
 *         则原样拷贝返回（降级为按原字符串分组）。
 */
size_t pinyin_of_name(const char *utf8_name, char *out, size_t out_len);
