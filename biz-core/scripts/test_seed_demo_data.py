"""验证演示流水遵循后端的金额与余额约定。"""

from datetime import datetime, timezone
import sqlite3
import unittest

from seed_demo_data import create_schema, populate


class DemoWalletTests(unittest.TestCase):
    def test_wallet_amounts_and_balances(self):
        with sqlite3.connect(":memory:") as db:
            db.execute("PRAGMA foreign_keys=ON")
            create_schema(db)
            populate(db, datetime(2026, 9, 9, 12, tzinfo=timezone.utc), 90, 20260909)
            debits = db.execute(
                "SELECT w.amount_fen,o.amount_fen,w.user_id,o.user_id "
                "FROM wallet_transactions w JOIN orders o ON o.id=w.order_id "
                "WHERE w.type='charge_debit'"
            ).fetchall()
            self.assertTrue(debits)
            for amount, order_amount, user, order_user in debits:
                self.assertGreater(amount, 0)
                self.assertEqual(amount, order_amount)
                self.assertEqual(user, order_user)
            for user, final_balance in db.execute("SELECT id,balance_fen FROM users"):
                balance = 0
                for kind, amount, balance_after in db.execute(
                    "SELECT type,amount_fen,balance_after_fen FROM wallet_transactions "
                    "WHERE user_id=? ORDER BY created_at,id", (user,)
                ):
                    self.assertGreater(amount, 0)
                    balance += amount if kind == "top_up" else -amount
                    self.assertEqual(balance, balance_after)
                    self.assertGreaterEqual(balance, 0)
                self.assertEqual(balance, final_balance)
        db.close()


if __name__ == "__main__":
    unittest.main()
