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

#include <libyul/YulName.h>

#include <libyul/optimiser/NameCollector.h>
#include <libyul/optimiser/OptimizerUtilities.h>

#include <libyul/AST.h>
#include <libyul/Exceptions.h>

#include <fmt/compile.h>

#include <range/v3/algorithm/max.hpp>
#include <range/v3/view/map.hpp>

using namespace solidity::yul;

namespace
{

auto constexpr g_ghostPlaceholder = "GHOST[@]";
auto constexpr g_ghostPlaceholderFmt = "GHOST[{}]";

bool isInvalidLabel(
	std::string_view const _label,
	std::set<std::string, std::less<>> const& _reservedLabels,
	Dialect const& _dialect
)
{
	return isRestrictedIdentifier(_dialect, _label) || _reservedLabels.contains(_label);
}
}

YulNameLabelRegistry::YulNameLabelRegistry(): m_labels{"", g_ghostPlaceholder}, m_nameToLabelMapping{0, 1} {}

YulNameLabelRegistry::YulNameLabelRegistry(YulNameLabels const& _nameLabels): YulNameLabelRegistry()
{
	auto const& labelToNameMapping = _nameLabels.labelToNameMapping();
	yulAssert(labelToNameMapping.contains(""));
	yulAssert(labelToNameMapping.at("") == 0);
	yulAssert(labelToNameMapping.contains(g_ghostPlaceholder));
	yulAssert(labelToNameMapping.find(g_ghostPlaceholder)->second == 1);

	// already contains empty and ghost due to default ctor call
	m_labels.resize(labelToNameMapping.size());
	m_nameToLabelMapping.resize(ranges::max(labelToNameMapping | ranges::views::values) + 1);
	for (auto const& [label, name]: labelToNameMapping)
	{
		// skip empty and ghost
		if (name < 2)
			continue;

		m_labels.emplace_back(label);
		m_nameToLabelMapping[name] = m_labels.size() - 1;
	}
}

YulNameLabelRegistry::YulNameLabelRegistry(std::vector<std::string> const& _labels, std::vector<size_t> const& _nameToLabelMapping)
{
	yulAssert(_labels.size() >= 2);
	yulAssert(_labels[0] == "");
	yulAssert(_labels[1] == g_ghostPlaceholder);
	yulAssert(_nameToLabelMapping.size() >= 2);
	yulAssert(_nameToLabelMapping[0] == 0);
	yulAssert(_nameToLabelMapping[1] == 1);
	std::vector<uint8_t> labelVisited (_labels.size(), false);
	for (auto const& name: _nameToLabelMapping)
	{
		yulAssert(name < _labels.size());
		// it is possible to have multiple references to empty / ghost
		yulAssert(name < 2 || !labelVisited[name], fmt::format("YulName {} is not unique.", name));
		labelVisited[name] = true;
	}
	m_labels = _labels;
	m_nameToLabelMapping = _nameToLabelMapping;
}

size_t YulNameLabelRegistry::nameToLabelIndex(YulName const _name) const
{
	yulAssert(_name < m_nameToLabelMapping.size());
	return m_nameToLabelMapping[_name];
}

std::string_view YulNameLabelRegistry::operator()(YulName const _name) const
{
	auto const labelIndex = nameToLabelIndex(_name);
	if (labelIndex == ghostName())
		return lookupGhost(_name);
	return m_labels[nameToLabelIndex(_name)];
}

std::optional<YulNameLabelRegistry::YulName> YulNameLabelRegistry::findNameForLabel(std::string_view const _label) const {
	if (_label.empty())
		return emptyName();
	if (_label == g_ghostPlaceholder)
		return YulName{1};
	for (YulName name = 2; name <= maximumNameId(); ++name)
		if ((*this)(name) == _label)
			return name;
	return std::nullopt;
}

std::string_view YulNameLabelRegistry::lookupGhost(YulName const _name) const
{
	yulAssert(nameToLabelIndex(_name) == ghostName());
	auto const [it, _] = m_ghostLabelCache.try_emplace(_name, fmt::format(g_ghostPlaceholderFmt, _name));
	return it->second;
}

YulNameLabels::YulNameLabels(): m_labelToNameMapping{{"", 0}, {g_ghostPlaceholder, 1}} {}

std::tuple<YulNameLabelRegistry::YulName, bool> YulNameLabels::tryInsertLabelForName(std::string_view const _label, YulNameLabelRegistry::YulName const _name)
{
	yulAssert(_label != g_ghostPlaceholder);
	auto const [it, emplaced] = m_labelToNameMapping.try_emplace(std::string{_label}, _name);
	return std::make_tuple(it->second, emplaced);
}

YulNameLabelRegistryBuilder::YulNameLabelRegistryBuilder():
	m_nextNameId(2)
{}

YulNameLabelRegistryBuilder::YulNameLabelRegistryBuilder(YulNameLabelRegistry const& _existingLabels)
{
	for (size_t i = 2; i <= _existingLabels.maximumNameId(); ++i)
	{
		auto const existingLabel = _existingLabels(i);
		if (!existingLabel.empty())
		{
			auto const [_, inserted] = m_configuration.tryInsertLabelForName(_existingLabels(i), i);
			yulAssert(inserted);
		}
	}
	m_nextNameId = _existingLabels.maximumNameId() + 1;
}

YulNameLabelRegistry::YulName YulNameLabelRegistryBuilder::define(std::string_view const _label)
{
	auto const [nameId, inserted] = m_configuration.tryInsertLabelForName(_label, m_nextNameId);
	if (inserted)
		m_nextNameId++;
	return nameId;
}

YulNameDispenser::YulNameDispenser(YulNameLabelRegistry const& _labelRegistry, std::set<std::string> const& _reservedLabels):
	m_registry(_labelRegistry),
	m_reservedLabels(_reservedLabels.begin(), _reservedLabels.end()),
	m_offset(_labelRegistry.maximumNameId() + 1)
{}

YulNameLabelRegistry::YulName YulNameDispenser::newName(YulNameLabelRegistry::YulName const parent)
{
	m_mapping.push_back(resolveBaseName(parent));
	return m_mapping.size() - 1 + m_offset;
}

YulNameLabelRegistry::YulName YulNameDispenser::newGhost()
{
	return newName(YulNameLabelRegistry::ghostName());
}

YulNameLabelRegistry::YulName YulNameDispenser::resolveBaseName(YulNameLabelRegistry::YulName _name) const
{
	if (_name >= m_offset)
		_name = m_mapping[_name - m_offset];
	yulAssert(_name < m_offset, "We have at most one level of indirection, this violates this assumption");
	return _name;
}

YulNameLabelRegistry YulNameDispenser::generateNewLabels(Block const& _root, Dialect const& _dialect) const
{
	auto usedNames = NameCollector(_root).names();
	// add ghosts to used names as they're not referenced in the regular ast
	for (size_t i = 0; i < m_mapping.size(); ++i)
		if (m_mapping[i] == YulNameLabelRegistry::ghostName())
			usedNames.insert(i + m_offset);

	if (usedNames.empty())
		return {};

	auto const& originalLabels = m_registry.get().labels();

	std::vector<uint8_t> reusedLabels (originalLabels.size());
	// this means that everything that is derived from empty / ghost needs to be generated
	reusedLabels[0] = true;
	reusedLabels[1] = true;

	std::vector<std::string> labels{"", g_ghostPlaceholder};
	labels.reserve(originalLabels.size()+2);
	// this is fine as used names is guaranteed to be not empty
	std::vector<size_t> nameToLabelMap(*std::prev(usedNames.end()) + 1, 0);
	nameToLabelMap[0] = 0;
	nameToLabelMap[1] = 1;

	std::set<std::string, std::less<>> alreadyDefinedNames = m_reservedLabels + std::set{"", g_ghostPlaceholder};

	std::vector<YulName> toGenerate;
	// filter out straightforward case: we just use whatever label was already there and put it into alreadyDefinedNames
	// otherwise it goes into the toGenerate collection
	for (auto const& name: usedNames)
	{
		auto const baseName = resolveBaseName(name);
		auto const baseLabelIndex = m_registry.get().nameToLabelIndex(baseName);
		auto const& baseLabel = originalLabels[baseLabelIndex];
		// if we haven't already reused the label, check that either the name didn't change, then we can just
		// take over the old label, otherwise check that it is a valid label and then reuse
		if (!reusedLabels[baseLabelIndex] && (baseName == name || !isInvalidLabel(baseLabel, m_reservedLabels, _dialect)))
		{
			labels.push_back(baseLabel);
			nameToLabelMap[name] = labels.size() - 1;
			alreadyDefinedNames.insert(baseLabel);
			reusedLabels[baseLabelIndex] = true;
		}
		else
			toGenerate.push_back(name);
	}

	for (auto const& name: toGenerate)
	{
		auto const baseName = resolveBaseName(name);

		// ghost variables get special treatment
		if (baseName == YulNameLabelRegistry::ghostName())
		{
			nameToLabelMap[name] = YulNameLabelRegistry::ghostName();
			continue;
		}

		auto const baseLabelIndex = m_registry.get().nameToLabelIndex(baseName);
		auto const& baseLabel = originalLabels[baseLabelIndex];

		std::string generatedLabel = baseLabel;
		size_t suffix = 1;
		do
		{
			generatedLabel = format(FMT_COMPILE("{}_{}"), baseLabel, suffix++);
		} while (isInvalidLabel(generatedLabel, alreadyDefinedNames, _dialect));

		labels.push_back(generatedLabel);
		nameToLabelMap[name] = labels.size() - 1;
		alreadyDefinedNames.insert(generatedLabel);
	}

	return YulNameLabelRegistry{labels, nameToLabelMap};
}
