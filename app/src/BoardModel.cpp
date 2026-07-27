#include "BoardModel.h"

#include <QJsonObject>

BoardModel::BoardModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int BoardModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_squares.size());
}

QVariant BoardModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_squares.size())
        return {};

    const Square &square = m_squares.at(index.row());
    switch (role) {
    case SquareNameRole:
        return square.name;
    case LightRole:
        return square.light;
    case PieceRole:
        return square.piece;
    case FootprintRole:
        return square.footprint;
    default:
        return {};
    }
}

QHash<int, QByteArray> BoardModel::roleNames() const
{
    return {
        {SquareNameRole, "squareName"},
        {LightRole, "light"},
        {PieceRole, "piece"},
        {FootprintRole, "footprint"},
    };
}

void BoardModel::applySquares(const QJsonArray &squares)
{
    beginResetModel();
    m_squares.clear();
    m_squares.reserve(squares.size());
    for (const QJsonValue &value : squares) {
        const QJsonObject object = value.toObject();
        m_squares.append(Square{
            object.value(QStringLiteral("name")).toString(),
            object.value(QStringLiteral("light")).toBool(),
            object.value(QStringLiteral("piece")).toString(),
            object.value(QStringLiteral("footprint")).toString(),
        });
    }
    endResetModel();
}
