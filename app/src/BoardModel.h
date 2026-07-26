#pragma once

#include <QAbstractListModel>
#include <QJsonArray>

// The squares the core told us to draw, in the order it told us to draw them.
//
// This model is filled only from core events. It holds no chess knowledge:
// it cannot compute a starting position, place a piece, or flip a board.
class BoardModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        SquareNameRole = Qt::UserRole + 1,
        LightRole,
        PieceRole,
    };

    explicit BoardModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Replaces every square with the squares carried by a core event.
    void applySquares(const QJsonArray &squares);

private:
    struct Square {
        QString name;
        bool light = false;
        QString piece; // empty when the square is empty
    };

    QList<Square> m_squares;
};
