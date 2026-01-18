import json

def generate_item(i):
    return {
        "id": i,
        "name": f"item_{i}",
        "active": i % 2 == 0,
        "metadata": {
            "created_at": "2025-01-18T12:00:00Z",
            "updated_at": "2025-01-18T15:30:00Z",
            "version": i % 100,
            "tags": ["tag1", "tag2", "tag3"],
            "settings": {
                "enabled": True,
                "priority": i % 10,
                "config": {
                    "timeout": 3000,
                    "retries": 3,
                    "options": ["opt_a", "opt_b"]
                }
            }
        },
        "user": {
            "user_id": i * 10,
            "username": f"user_{i}",
            "profile": {
                "email": f"user_{i}@example.com",
                "phone": "+1234567890",
                "address": {
                    "city": "Moscow",
                    "country": "Russia",
                    "zip": "123456"
                }
            }
        },
        "data": {
            "value": i * 1.5,
            "description": "A" * 200
        }
    }

output_path = r"D:\cpp\json_parser\gen_dataset.json"
target_size = 1.5 * 1024 * 1024 * 1024  # 2 GB

with open(output_path, 'w') as f:
    f.write('[\n')
    i = 0
    current_size = 2
    
    while current_size < target_size:
        item = generate_item(i)
        item_json = json.dumps(item)
        
        if i > 0:
            f.write(',\n')
            current_size += 2
        
        f.write(item_json)
        current_size += len(item_json)
        i += 1
        
        if i % 100000 == 0:
            print(f"Progress: {current_size / (1024**3):.2f} GB, {i} items")
    
    f.write('\n]')

print(f"Done! Total items: {i}")