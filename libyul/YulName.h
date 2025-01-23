/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0

#pragma once

#include <libyul/YulString.h>

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace solidity::yul
{

struct Block;
class Dialect;
struct FunctionDefinition;
struct Identifier;

class YulNameLabels;
class YulNameLabelRegistry
{
public:
	/// It is unsafe to use a YulNodeId from a different registry instance, and it is up to the user to safeguard
	/// against this.
	using YulName = size_t;

	YulNameLabelRegistry();
	explicit YulNameLabelRegistry(YulNameLabels const& _nameLabels);
	explicit YulNameLabelRegistry(std::vector<std::string> const& _labels, std::vector<size_t> const& _nameToLabelMapping);
	YulNameLabelRegistry(YulNameLabelRegistry const&) = default;
	YulNameLabelRegistry(YulNameLabelRegistry&&) = default;
	~YulNameLabelRegistry() = default;
	YulNameLabelRegistry& operator=(YulNameLabelRegistry const&) = default;
	YulNameLabelRegistry& operator=(YulNameLabelRegistry&&) = default;

	std::string_view operator()(YulName _name) const;

	static bool constexpr empty(YulName const _name) { return _name == emptyName(); }
	static YulName constexpr emptyName() { return 0; }
	static YulName constexpr ghostName() { return 1; }

	std::vector<std::string> const& labels() const { return m_labels; }
	YulName maximumNameId() const { return m_nameToLabelMapping.size() - 1; }

	size_t nameToLabelIndex(YulName _name) const;
	/// this is a potentially expensive operation
	std::optional<YulName> findNameForLabel(std::string_view _label) const;
private:
	std::string_view lookupGhost(YulName _name) const;

	std::vector<std::string> m_labels;
	std::vector<size_t> m_nameToLabelMapping;
	mutable std::map<YulName, std::string> m_ghostLabelCache;
};

class YulNameLabels
{
public:
	YulNameLabels();
	std::tuple<YulNameLabelRegistry::YulName, bool> tryInsertLabelForName(std::string_view _label, YulNameLabelRegistry::YulName _name);

	auto const& labelToNameMapping() const { return m_labelToNameMapping; }
private:
	std::map<std::string, size_t, std::less<>> m_labelToNameMapping;
};

class YulNameLabelRegistryBuilder
{
public:
	YulNameLabelRegistryBuilder();
	explicit YulNameLabelRegistryBuilder(YulNameLabelRegistry const& _existingLabels);
	YulNameLabelRegistry::YulName define(std::string_view _label);
	YulNameLabelRegistry build() const { return YulNameLabelRegistry{m_configuration}; }
private:
	YulNameLabels m_configuration;
	size_t m_nextNameId = 0;
};

class YulNameDispenser
{
public:
	explicit YulNameDispenser(
		YulNameLabelRegistry const& _labelRegistry,
		std::set<std::string> const& _reservedLabels = {}
	);

	YulNameLabelRegistry const& labels() const { return m_registry.get();}

	YulNameLabelRegistry::YulName newName(YulNameLabelRegistry::YulName parent = 0);
	YulNameLabelRegistry::YulName newGhost();
	YulNameLabelRegistry generateNewLabels(Block const& _root, Dialect const& _dialect) const;
private:
	YulNameLabelRegistry::YulName resolveBaseName(YulNameLabelRegistry::YulName _name) const;

	std::reference_wrapper<YulNameLabelRegistry const> m_registry;
	std::set<std::string, std::less<>> m_reservedLabels;
	size_t m_offset;
	std::vector<YulNameLabelRegistry::YulName> m_mapping;
};

using YulName = YulNameLabelRegistry::YulName;

}
