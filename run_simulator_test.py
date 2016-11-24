import sys
import json

def main():
    filename = sys.argv[1]

    with open(filename) as fin:
        data = json.load(fin)

    width = data['width']
    height = data['height']
    num_frames = data['num_frames']

    assert len(data['moves']) == num_frames - 1
    assert len(data['frames']) == num_frames

    for rect in data['moves'][0], data['frames'][0]:
        assert len(rect) == height
        assert len(rect[0]) == width

    for i in range(num_frames - 1):
        print(width, height)
        print('production')
        for y in range(height):
            for x in range(width):
                print('{:2}'.format(data['productions'][y][x]), end=' ')
            print()

        frame = data['frames'][i]
        print('owner')
        for y in range(height):
            for x in range(width):
                print(frame[y][x][0], end=' ')
            print()

        print('strength')
        for y in range(height):
            for x in range(width):
                print('{:3}'.format(frame[y][x][1]), end=' ')
            print()

        print('moves')
        for y in range(height):
            for x in range(width):
                print(data['moves'][i][y][x], end=' ')
            print()

        next_frame = data['frames'][i + 1]
        print('next_owner')
        for y in range(height):
            for x in range(width):
                print(next_frame[y][x][0], end=' ')
            print()

        print('next_strength')
        for y in range(height):
            for x in range(width):
                print('{:3}'.format(next_frame[y][x][1]), end=' ')
            print()


if __name__ == '__main__':
    main()

