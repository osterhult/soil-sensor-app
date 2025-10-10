class Core:
    """Minimal stub validator used for west board enumeration."""

    def __init__(self, source_data=None, schema_data=None):
        self.source_data = source_data
        self.schema_data = schema_data

    def validate(self):
        return True
