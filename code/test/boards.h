#pragma once

#include <array>
#include "algo/probability_engine/observed_board/observed_board.h"

namespace test {

inline mss::ObservedBoard::Result bruteForceBoard(bool independent) {
    constexpr int kRows = 30;
    constexpr int kCols = 16;
    constexpr int kMines = 99;
    // 固定残局沿用 1.4；合法布局数应为 134550，最佳单步胜局数应为 83553。
    constexpr std::array<const char *, kRows> kBoardRows{
        {"00012?211111012?", "0001?33?11?101??", "112222?33321013?", "2?2?1123??11223?", "?332101?4321??3?", "12?100112?11222?",
         "0111000011100011", "111000000011112?", "1?210011113?33??", "12?2122?22???6??", "0112?3?33?45????", "00024?5?32?224??",
         "0001??4?32222332", "110122213?32??10", "?11110113??22210", "223?201?22221111", "1?3?20112112?21?", "112110001?12?211",
         "0122111122322111", "12??11?22?4?201?", "?333112?44??2011", "23?21012??332211", "?23?21122211?2?1", "222?32?211232211",
         "?223??3?11??2111", "?3?????22233?11?", "???????22?211111", "???????33?311011", "????????323?101?", "????????21?21011"}};

    mss::ObservedBoard::Result board(kRows + (independent ? 4 : 0), kCols, kMines + independent);
    if (independent) {
        // 新区域以 ForcedSafe 隔开；新增两格恰有一雷，点击任一安全格只揭示 1，不提供原残局的信息。
        for (int row = kRows; row < kRows + 4; ++row)
            for (int col = 0; col < kCols; ++col)
                board.board[row][col] = mss::ObservedBoard::CellState::ForcedSafe;
        board.board[kRows + 2][0] = mss::ObservedBoard::CellState::Hidden;
        board.board[kRows + 2][1] = mss::ObservedBoard::CellState::Hidden;
        board.board[kRows + 3][0] = mss::ObservedBoard::CellState::Num1;
    }
    for (int row = 0; row < kRows; ++row)
        for (int col = 0; col < kCols; ++col) {
            const char cell = kBoardRows[row][col];
            board.board[row][col] = cell == '?' ? mss::ObservedBoard::CellState::Hidden : (mss::ObservedBoard::CellState)(cell - '0');
        }

    return board;
}

inline mss::ObservedBoard::Result monteCarloBoard() {
    mss::ObservedBoard::Result board(9, 9, 10);
    board.board[0][0] = board.board[0][8] = board.board[8][0] = board.board[8][8] = mss::ObservedBoard::CellState::Num1;
    return board;
}

}
