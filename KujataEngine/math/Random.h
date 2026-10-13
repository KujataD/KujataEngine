#pragma once
#include <numbers>
#include <random>
#include <type_traits>
#include <algorithm>

#include "../runtime/KujataApi.h"

namespace KujataEngine {

/// <summary>
/// 見た目だけの乱数(パーティクル・演出のばらつきなど)。起動するたびに種が変わる。
/// ゲームの判定・動きには使わない(毎回違う結果になり、不具合の再現やリプレイができなくなる。.claude/determinism.md)。
/// GameModule からも使えるよう、KUJATA_API で書き出す(乱数の状態はエンジン側の 1 つを共有する)。
/// </summary>
class KUJATA_API Random {
public:
	static void Initialize() {
		// 乱数生成エンジン
		std::random_device seedGenerator;
		// メルセンヌ·ツイスターエンジンの初期化
		randomEngine.seed(seedGenerator());
	}

	template<typename T> static T GetRandom(T min, T max) {
		// スワップ
		if (min > max) {
			std::swap(min, max);
		}

		// 整数型か少数型かどうか
		if constexpr (std::is_integral_v<T>) {
			std::uniform_int_distribution<T> distribution(min, max);
			return distribution(randomEngine);
		} else if constexpr (std::is_floating_point_v<T>) {
			std::uniform_real_distribution<T> distribution(min, max);
			return distribution(randomEngine);
		} else {
			return min;
		}
	}

private:
	// メルセンヌ·ツイスターエンジン(64bit版)
	static std::mt19937_64 randomEngine;
};

} // namespace KujataEngine