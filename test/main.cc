namespace etch::tests {
void registerParseTests();
void registerTnfaTests();
}  // namespace etch::tests

int main() {
    etch::tests::registerParseTests();
    etch::tests::registerTnfaTests();
    return 0;
}
