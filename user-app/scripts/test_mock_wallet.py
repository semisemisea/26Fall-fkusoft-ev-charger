"""验证旧 mock 结算流水也使用金额大小与交易类型。"""

import unittest
from unittest.mock import Mock, patch

import mock_server


class MockWalletTests(unittest.TestCase):
    def test_settlement_records_magnitude_and_debits_once(self):
        for amount in (1234, 0):
            with self.subTest(amount=amount):
                user = {"id": 1, "walletBalanceFen": 5000}
                order = {"id": 1, "userId": 1, "stationId": 1,
                         "chargerId": 11, "status": "awaiting_payment",
                         "amountFen": amount}
                handler = Mock()
                handler.require_user.return_value = user
                with patch.object(mock_server, "orders", {1: order}), \
                     patch.object(mock_server, "wallet_transactions", {}), \
                     patch.object(mock_server, "next_transaction_id", 1):
                    mock_server.Handler.handle_settle_order(handler, 1)
                    mock_server.Handler.handle_settle_order(handler, 1)
                    handler.send_error_code.assert_not_called()
                    self.assertEqual(user["walletBalanceFen"], 5000 - amount)
                    transactions = mock_server.wallet_transactions[1]
                    self.assertEqual(len(transactions), 1)
                    self.assertEqual(transactions[0]["type"], "charge_debit")
                    self.assertEqual(transactions[0]["amountFen"], amount)
                    self.assertEqual(transactions[0]["balanceAfterFen"], 5000 - amount)


if __name__ == "__main__":
    unittest.main()
