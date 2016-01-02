import filecmp

if not filecmp.cmp('tests/RNULinchar.txt', 'tests/ENULinchar.txt'):
	print('FAIL NUL char')
else:
	print('PASS NUL char')

if not filecmp.cmp('tests/RstartNUL.txt', 'tests/EstartNUL.txt'):
	print('FAIL long line')
else:
	print('PASS long line')

if not filecmp.cmp('tests/mainExpected.txt', 'tests/mainResults.txt'):
	print('FAIL main')
else:
	print('PASS main')