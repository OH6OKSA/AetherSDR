#include "RadioListModel.h"

namespace AetherSDR::mobile {

int RadioListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_radios.size());
}

QVariant RadioListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_radios.size()) {
        return {};
    }
    const DiscoveredRadio& radio = m_radios.at(index.row());
    switch (role) {
    case FamilyRole: return radio.family;
    case SerialRole: return radio.serial;
    case NameRole: return radio.name;
    case ModelRole: return radio.model;
    case NicknameRole: return radio.nickname;
    case VersionRole: return radio.version;
    case TransportRole: return radio.transport;
    case AddressRole: return radio.address;
    case InUseRole: return radio.inUse;
    default: return {};
    }
}

QHash<int, QByteArray> RadioListModel::roleNames() const
{
    return {
        {FamilyRole, "family"},
        {SerialRole, "serial"},
        {NameRole, "name"},
        {ModelRole, "model"},
        {NicknameRole, "nickname"},
        {VersionRole, "version"},
        {TransportRole, "transport"},
        {AddressRole, "address"},
        {InUseRole, "inUse"},
    };
}

const DiscoveredRadio* RadioListModel::radioAt(int row) const
{
    if (row < 0 || row >= m_radios.size()) {
        return nullptr;
    }
    return &m_radios.at(row);
}

void RadioListModel::upsert(const DiscoveredRadio& radio)
{
    const int row = indexOf(radio.family, radio.serial);
    if (row >= 0) {
        m_radios[row] = radio;
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed);
        return;
    }
    const int newRow = static_cast<int>(m_radios.size());
    beginInsertRows({}, newRow, newRow);
    m_radios.append(radio);
    endInsertRows();
    emit countChanged();
}

void RadioListModel::remove(const QString& family, const QString& serial)
{
    const int row = indexOf(family, serial);
    if (row < 0) {
        return;
    }
    beginRemoveRows({}, row, row);
    m_radios.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

int RadioListModel::indexOf(const QString& family, const QString& serial) const
{
    for (int i = 0; i < m_radios.size(); ++i) {
        if (m_radios.at(i).family == family && m_radios.at(i).serial == serial) {
            return i;
        }
    }
    return -1;
}

} // namespace AetherSDR::mobile
