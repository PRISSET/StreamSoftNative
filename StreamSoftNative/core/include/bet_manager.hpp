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
    // Returns the new "generation" for this match window. Bet resolution can
    // be delayed several minutes (waiting on Faceit API confirmation, see
    // overlay_server.hpp) — if a new match starts in the meantime, the old
    // resolution must not touch the new match's bets. Callers stash the
    // generation from open_window()/generation() and pass it back into
    // resolve()/void_all(), which no-op if a newer window has since opened.
    int open_window() {
        std::lock_guard<std::mutex> lock(mutex_);
        bets_.clear();
        open_ = true;
        return ++generation_;
    }

    void lock_window() {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = false;
    }

    bool is_open() {
        std::lock_guard<std::mutex> lock(mutex_);
        return open_;
    }

    int generation() {
        std::lock_guard<std::mutex> lock(mutex_);
        return generation_;
    }

    bool has_bets(int expected_generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        return expected_generation == generation_ && !bets_.empty();
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

    BetResolution resolve(int expected_generation, bool player_won, PointsStore& points, double multiplier) {
        std::unordered_map<std::string, Bet> bets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (expected_generation != generation_) return {};
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

    // Refunds every open stake and clears the window — used when a match
    // can't be confirmed as a real Faceit match (wrong lobby type, cancelled
    // match, GSI went stale without a clean end) so viewers aren't left
    // short over something that was never actually resolved.
    int void_all(int expected_generation, PointsStore& points) {
        std::unordered_map<std::string, Bet> bets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (expected_generation != generation_) return 0;
            bets = std::move(bets_);
            bets_.clear();
            open_ = false;
        }
        for (const auto& [username, bet] : bets) points.add(username, bet.amount);
        return static_cast<int>(bets.size());
    }

private:
    std::mutex mutex_;
    bool open_ = false;
    int generation_ = 0;
    std::unordered_map<std::string, Bet> bets_;
};

}
