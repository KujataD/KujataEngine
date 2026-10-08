#include "ActionState.h"

#include <algorithm>

namespace KujataEngine {

void ActionState::BeginStep() { previous_ = current_; }

void ActionState::SetValue(const std::string& name, int32_t x, int32_t y) {
	Value& value = current_[name];
	value.x = std::clamp(x, -kActionAxisMax, kActionAxisMax);
	value.y = std::clamp(y, -kActionAxisMax, kActionAxisMax);
}

ActionState::Value ActionState::Find(const std::unordered_map<std::string, Value>& values, const std::string& name) const {
	const auto found = values.find(name);
	return (found != values.end()) ? found->second : Value{};
}

bool ActionState::Held(const std::string& name) const {
	const Value value = Find(current_, name);
	return value.x != 0 || value.y != 0;
}

bool ActionState::Pressed(const std::string& name) const {
	const Value current = Find(current_, name);
	const Value prev = Find(previous_, name);
	return (current.x != 0 || current.y != 0) && prev.x == 0 && prev.y == 0;
}

bool ActionState::Released(const std::string& name) const {
	const Value current = Find(current_, name);
	const Value prev = Find(previous_, name);
	return current.x == 0 && current.y == 0 && (prev.x != 0 || prev.y != 0);
}

float ActionState::Axis1D(const std::string& name) const {
	return static_cast<float>(Find(current_, name).x) / static_cast<float>(kActionAxisMax);
}

Vector2 ActionState::Axis2D(const std::string& name) const {
	const Value value = Find(current_, name);
	return {static_cast<float>(value.x) / static_cast<float>(kActionAxisMax), static_cast<float>(value.y) / static_cast<float>(kActionAxisMax)};
}

int32_t ActionState::RawX(const std::string& name) const { return Find(current_, name).x; }

int32_t ActionState::RawY(const std::string& name) const { return Find(current_, name).y; }

void ActionState::Clear() {
	current_.clear();
	previous_.clear();
}

std::vector<std::string> ActionState::GetNames() const {
	std::vector<std::string> names;
	names.reserve(current_.size());
	for (const auto& entry : current_) {
		names.push_back(entry.first);
	}
	// 表示の順番を毎回同じにする(CUI の返事が安定する)。
	std::sort(names.begin(), names.end());
	return names;
}

ActionCommand::ActionCommand(std::string name, int32_t x, int32_t y) : name_(std::move(name)), x_(x), y_(y) {}

void ActionCommand::Execute(SimContext& context) {
	if (context.actions) {
		context.actions->SetValue(name_, x_, y_);
	}
}

void ActionCommand::Write(nlohmann::json& out) const {
	out["name"] = name_;
	out["x"] = x_;
	out["y"] = y_;
}

void ActionCommand::Read(const nlohmann::json& in) {
	if (in.contains("name") && in.at("name").is_string()) {
		name_ = in.at("name").get<std::string>();
	}
	if (in.contains("x") && in.at("x").is_number_integer()) {
		x_ = in.at("x").get<int32_t>();
	}
	if (in.contains("y") && in.at("y").is_number_integer()) {
		y_ = in.at("y").get<int32_t>();
	}
}

} // namespace KujataEngine
