; GMP used to read a stored bigint exponent through its small-value field.
; Extreme exponents also need the correct directed rounding behavior.
(set-info :status unsat)
(simplify ((_ to_fp 11 53) RNE 1.0 (- 4294967313 4294967296)))
(simplify ((_ to_fp 11 53) RNE 1.0 (- 2147483649)))
(simplify ((_ to_fp 11 53) RTZ 1.0 18446744073709551616))
(assert (fp.isInfinite ((_ to_fp 11 53) RNE 1.0 (- 4294967313 4294967296))))
(check-sat)
