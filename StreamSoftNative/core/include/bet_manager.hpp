#pragma once

#include "points.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace streamsoft::cs2 {

struct Bet {
    std::string side;
    int amount = 0;
};

struct BetResolution {
    int winners = 0;
    int losers = 0;
    long long paid_out = 0;
};

// Fixed-odds viewer betting on the streamer's own current CS2 match — a
// correct bet returns amount*multiplier, a wrong one loses the stake. No
// shared pool math (no "empty pool" edge case): every bet settles against
// the streamer's own points balance, not other viewers'.
class BetManager {
public:
    void open_window() {
        std::lock_guard<std::mutex> lock(mutex_);
        bets_.clear();
        open_ = true;
    }

    void lock_window() {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = false;
    }

    bool is_open() {
        std::lock_guard<std::mutex> lock(mutex_);
        return open_;
    }

    std::optional<std::string> place_bet(const std::string& username, const std::string& side, int amount,
                                          PointsStore& points, int min_amount, int max_amount) {
        if (side != "win" && side != "lose") {
            return std::string("Использование: !bet win <баллы> или !bet lose <баллы>");
        }
        if (amount < min_amount || amount > max_amount) {
            return username + ", ставка должна быть от " + std::to_string(min_amount) + " до " +
                   std::to_string(max_amount) + " баллов";
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_) return username + ", ставки сейчас закрыты";
        if (bets_.count(username)) return username + ", ты уже поставил в этом матче";

        if (!points.spend(username, amount)) {
            return username + ", недостаточно баллов (у тебя " + std::to_string(points.balance(username)) + ")";
        }

        bets_[username] = {side, amount};
        return username + " поставил " + std::to_string(amount) + " на " +
               (side == "win" ? std::string("победу") : std::string("поражение"));
    }

    BetResolution resolve(bool player_won, PointsStore& points, double multiplier) {
        std::unordered_map<std::string, Bet> bets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bets = std::move(bets_);
            bets_.clear();
            open_ = false;
        }

        BetResolution r;
        std::string winning_side = player_won ? "win" : "lose";
        for (const auto& [username, bet] : bets) {
            if (bet.side == winning_side) {
                int payout = static_cast<int>(bet.amount * multiplier);
                points.add(username, payout);
                r.winners++;
                r.paid_out += payout;
            } else {
                r.losers++;
            }
        }
        return r;
    }

private:
    std::mutex mutex_;
    bool open_ = false;
    std::unordered_map<std::string, Bet> bets_;
};

}
